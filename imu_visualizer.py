"""
LSM6DSOX  //  Cyberpunk IMU Visualizer
pip install pyserial pygame
Serial mode:  python imu_visualizer.py [PORT] [BAUD]
UDP mode:     python imu_visualizer.py udp [PORT_NUMBER]
"""
import re, sys, threading, time, math, socket
import pygame, psutil, os
import serial
from collections import deque

_proc = psutil.Process(os.getpid())

UDP_MODE = len(sys.argv) > 1 and sys.argv[1].lower() == "udp"
UDP_PORT = int(sys.argv[2]) if UDP_MODE and len(sys.argv) > 2 else 4210
PORT     = sys.argv[1] if not UDP_MODE and len(sys.argv) > 1 else "COM4"
BAUD     = int(sys.argv[2]) if not UDP_MODE and len(sys.argv) > 2 else 115200
N       = 400   # rolling sample count

# ── palette ───────────────────────────────────────────────────
BG      = (  4,   4,  14)
PANEL   = (  8,   8,  22)
GRID    = ( 14,  14,  38)
BORDER  = ( 30,  30,  80)
DIM     = ( 50,  60,  90)
WHITE   = (220, 230, 255)

AX_COL   = (  0, 255, 220)
AY_COL   = (255,  50, 200)
AZ_COL   = ( 50, 255, 100)
GX_COL   = (255, 120,   0)
GY_COL   = (160,  50, 255)
GZ_COL   = (255, 200,   0)
CUBE_COL = (  0, 200, 255)

# ── shared state ──────────────────────────────────────────────
bufs     = {k: deque([0.0]*N, N) for k in ('ax','ay','az','gx','gy','gz')}
latest   = {k: 0.0 for k in ('ax','ay','az','gx','gy','gz','temp')}
attitude = {'roll': 0.0, 'pitch': 0.0, 'yaw': 0.0}
status   = {'conn': False, 'hz': 0.0}
last_t   = [time.monotonic()]
lock     = threading.Lock()
ALPHA    = 0.96

def update_attitude(ax, ay, az, gx, gy, gz):
    now = time.monotonic()
    dt  = min(now - last_t[0], 0.1)
    last_t[0] = now
    acc_roll  = math.atan2(ay, az)
    acc_pitch = math.atan2(-ax, math.sqrt(ay*ay + az*az))
    with lock:
        attitude['roll']  = ALPHA*(attitude['roll']  + gx*dt) + (1-ALPHA)*acc_roll
        attitude['pitch'] = ALPHA*(attitude['pitch'] + gy*dt) + (1-ALPHA)*acc_pitch
        attitude['yaw']  += gz * dt

def _ingest(raw, t0_ref, frames_ref):
    """Parse one line of CSV and push into shared state. Returns new frame count."""
    parts = raw.split(',')
    try:
        if len(parts) >= 6:
            vals = [float(x) for x in parts[:6]]
            tmp  = float(parts[6]) if len(parts) >= 7 else None
        else:
            nums = re.findall(r'[+-]?\d+\.\d+', raw)
            if len(nums) >= 6:
                vals = [float(x) for x in nums[:6]]
                tmp  = float(nums[6]) if len(nums) >= 7 else None
            else:
                m = re.search(
                    r'Accel:.*?x=([\d.-]+)\s+y=([\d.-]+)\s+z=([\d.-]+)'
                    r'.*?Gyro:.*?x=([\d.-]+)\s+y=([\d.-]+)\s+z=([\d.-]+)', raw)
                if not m: return frames_ref
                vals = [float(m.group(i)) for i in range(1, 7)]
                tm2  = re.search(r'Temp:\s*([\d.-]+)', raw)
                tmp  = float(tm2.group(1)) if tm2 else None
        update_attitude(*vals)
        with lock:
            for k, v in zip(('ax','ay','az','gx','gy','gz'), vals):
                bufs[k].append(v); latest[k] = v
            if tmp is not None: latest['temp'] = tmp
            frames_ref += 1
            elapsed = time.monotonic() - t0_ref[0]
            if elapsed >= 0.5:
                status['hz'] = frames_ref / elapsed
                frames_ref, t0_ref[0] = 0, time.monotonic()
    except ValueError:
        pass
    return frames_ref

def serial_reader():
    while True:
        try:
            ser = serial.Serial(PORT, BAUD, timeout=0.5)
            with lock: status['conn'] = True
            t0, frames = [time.monotonic()], 0
            while True:
                raw = ser.readline().decode('utf-8', errors='ignore').strip()
                frames = _ingest(raw, t0, frames)
        except serial.SerialException:
            with lock: status['conn'] = False
            time.sleep(1)

def udp_reader():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', UDP_PORT))
    sock.settimeout(2.0)
    print(f"[UDP] Listening on port {UDP_PORT}")
    t0, frames = [time.monotonic()], 0
    while True:
        try:
            data, _ = sock.recvfrom(256)
            with lock:
                status['conn'] = True
            raw = data.decode('utf-8', errors='ignore').strip()
            frames = _ingest(raw, t0, frames)
        except socket.timeout:
            with lock: status['conn'] = False

if UDP_MODE:
    threading.Thread(target=udp_reader,  daemon=True).start()
else:
    threading.Thread(target=serial_reader, daemon=True).start()

# ── pygame ────────────────────────────────────────────────────
pygame.init()
W, H = 1280, 720
screen = pygame.display.set_mode((W, H))
pygame.display.set_caption("LSM6DSOX  //  IMU  VISUALIZER")
clock  = pygame.time.Clock()

try:
    font_b = pygame.font.SysFont("Consolas", 13, bold=True)
    font_m = pygame.font.SysFont("Consolas", 11)
    font_s = pygame.font.SysFont("Consolas",  9)
except Exception:
    font_b = pygame.font.SysFont("Courier New", 13, bold=True)
    font_m = pygame.font.SysFont("Courier New", 11)
    font_s = pygame.font.SysFont("Courier New",  9)

# pre-allocated surfaces
_glow     = pygame.Surface((W, H), pygame.SRCALPHA)
_scope_s  = pygame.Surface((W, H), pygame.SRCALPHA)

# pre-render scanlines overlay (CRT feel)
_scanlines = pygame.Surface((W, H), pygame.SRCALPHA)
for row in range(0, H, 3):
    pygame.draw.line(_scanlines, (0, 0, 0, 35), (0, row), (W, row))

# pre-render vignette
_vignette = pygame.Surface((W, H), pygame.SRCALPHA)
cx_v, cy_v = W//2, H//2
for vy in range(H):
    for vx in range(0, W, 4):   # sample every 4px — fast enough
        dx = (vx - cx_v) / cx_v
        dy = (vy - cy_v) / cy_v
        a  = int(max(0, min(180, (dx*dx + dy*dy) * 160)))
        _vignette.set_at((vx, vy), (0,0,0,a))

# ── 3D cube ───────────────────────────────────────────────────
_VERTS = [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),
          (-1,-1, 1),(1,-1, 1),(1,1, 1),(-1,1, 1)]
_EDGES = [(0,1),(1,2),(2,3),(3,0),
          (4,5),(5,6),(6,7),(7,4),
          (0,4),(1,5),(2,6),(3,7)]

def rotate_v(v, roll, pitch, yaw):
    x, y, z = v
    ca, sa = math.cos(roll),  math.sin(roll)
    y, z   = y*ca-z*sa, y*sa+z*ca
    ca, sa = math.cos(pitch), math.sin(pitch)
    x, z   = x*ca+z*sa, -x*sa+z*ca
    ca, sa = math.cos(yaw),   math.sin(yaw)
    x, y   = x*ca-y*sa, x*sa+y*ca
    return (x, y, z)

def perspective(v, cx, cy, scale=80):
    x, y, z = v
    d = z + 5
    if abs(d) < 0.001: d = 0.001
    return (int(cx + x*scale/d*4), int(cy - y*scale/d*4))

def draw_cube(cx, cy, roll, pitch, yaw):
    vs3d   = [rotate_v(v, roll, pitch, yaw) for v in _VERTS]
    pts    = [perspective(v, cx, cy) for v in vs3d]
    depths = [v[2]+5 for v in vs3d]

    # batched glow: one blit per pass, all edges drawn together
    for w, a in ((9,12),(5,40),(3,110),(1,255)):
        _glow.fill((0,0,0,0))
        for i, j in _EDGES:
            d = (depths[i]+depths[j]) / 2
            t = max(0.3, min(1.0, (d-2)/4))
            c = (int(CUBE_COL[0]*t), int(CUBE_COL[1]*t), int(CUBE_COL[2]*t))
            pygame.draw.line(_glow, (*c, a), pts[i], pts[j], w)
        screen.blit(_glow, (0,0))

    for pt in pts:
        pygame.draw.circle(screen, CUBE_COL, pt, 3)

# ── artificial horizon ────────────────────────────────────────
def draw_horizon(cx, cy, r, roll, pitch):
    size = r * 3
    hs   = pygame.Surface((size, size), pygame.SRCALPHA)
    mid  = size // 2
    pp   = int(math.sin(pitch) * r * 1.2)

    hs.fill((10, 20, 70, 210),  (0, 0, size, mid - pp))
    hs.fill((55, 30,  8, 210),  (0, mid - pp, size, size))
    pygame.draw.line(hs, (180, 180, 180, 200), (0, mid-pp), (size, mid-pp), 2)

    for deg in (-20, -10, 10, 20):
        ty = mid - pp - int(deg / 90 * r * 1.5)
        lw = r//3 if abs(deg) == 20 else r//5
        pygame.draw.line(hs, (100,100,100,150), (mid-lw, ty), (mid+lw, ty), 1)

    rotated  = pygame.transform.rotate(hs, math.degrees(roll))
    rw, rh   = rotated.get_size()

    tmp = pygame.Surface((r*2, r*2), pygame.SRCALPHA)
    tmp.blit(rotated, (r - rw//2, r - rh//2))

    mask = pygame.Surface((r*2, r*2), pygame.SRCALPHA)
    mask.fill((0,0,0,0))
    pygame.draw.circle(mask, (255,255,255,255), (r,r), r)
    tmp.blit(mask, (0,0), special_flags=pygame.BLEND_RGBA_MIN)

    screen.blit(tmp, (cx-r, cy-r))

    # rim + roll marker
    pygame.draw.circle(screen, (0, 180, 255), (cx,cy), r, 2)
    rx2 = int(cx + r * math.sin(-roll))
    ry2 = int(cy - r * math.cos(-roll))
    pygame.draw.circle(screen, (255, 200, 0), (rx2, ry2), 5)

    # centre reticle
    pygame.draw.line(screen, WHITE, (cx-14, cy), (cx-5, cy), 2)
    pygame.draw.line(screen, WHITE, (cx+5,  cy), (cx+14,cy), 2)
    pygame.draw.circle(screen, WHITE, (cx, cy), 3)

# ── oscilloscope ──────────────────────────────────────────────
def draw_scope(sx, sy, sw, sh, data_list, colors, lo, hi, title, units):
    pygame.draw.rect(screen, PANEL,  (sx, sy, sw, sh))
    pygame.draw.rect(screen, BORDER, (sx, sy, sw, sh), 1)

    for i in range(1, 4):
        yy = sy + i*sh//4
        pygame.draw.line(screen, GRID, (sx, yy), (sx+sw, yy))
    for i in range(1, 9):
        xx = sx + i*sw//8
        pygame.draw.line(screen, GRID, (xx, sy), (xx, sy+sh))

    zy = sy + int(sh * (-lo) / (hi - lo))
    pygame.draw.line(screen, (40, 40, 80), (sx, zy), (sx+sw, zy), 1)

    screen.blit(font_b.render(title, True, DIM),        (sx+8,    sy+5))
    screen.blit(font_s.render(units, True, (40,50,80)), (sx+sw-38, sy+5))
    screen.blit(font_s.render(f"{hi:+g}", True, DIM),   (sx+2,    sy+2))
    screen.blit(font_s.render(f"{lo:+g}", True, DIM),   (sx+2,    sy+sh-12))

    n = len(data_list[0])
    if n < 2: return
    step = sw / (n - 1)

    for w, a in ((4,25),(2,90),(1,230)):
        _scope_s.fill((0,0,0,0), (sx, sy, sw, sh))
        for data, color in zip(data_list, colors):
            pts = []
            for i, v in enumerate(data):
                px = int(sx + i * step)
                py = int(sy + sh - max(0.0, min(1.0, (v-lo)/(hi-lo))) * sh)
                pts.append((px, py))
            if len(pts) >= 2:
                pygame.draw.lines(_scope_s, (*color, a), False, pts, w)
        screen.blit(_scope_s, (0, 0))

def bracket(x, y, w, h, color, s=14, t=2):
    for px,py,sx2,sy2 in [(x,y,1,1),(x+w,y,-1,1),(x,y+h,1,-1),(x+w,y+h,-1,-1)]:
        pygame.draw.line(screen, color, (px,py), (px+sx2*s, py), t)
        pygame.draw.line(screen, color, (px,py), (px, py+sy2*s), t)

def legend(items, base_x, base_y):
    for i, (label, col) in enumerate(items):
        xx = base_x + i*46
        pygame.draw.line(screen, col, (xx, base_y), (xx+12, base_y), 2)
        screen.blit(font_m.render(label, True, col), (xx+15, base_y-6))

# ── layout constants ──────────────────────────────────────────
PAD    = 12
LEFT_W = 490
BAR_H  = 38

CUBE_CX = LEFT_W // 2
CUBE_CY = 210
CUBE_SC = 130   # bracket half-size

HOR_CX  = LEFT_W // 2
HOR_CY  = 530
HOR_R   = 90

SX  = LEFT_W + PAD
SW  = W - LEFT_W - PAD*2
SH  = (H - BAR_H - PAD*3) // 2
SAY = PAD
SGY = PAD*2 + SH

# ── main loop ─────────────────────────────────────────────────
running = True
while running:
    for ev in pygame.event.get():
        if ev.type == pygame.QUIT:
            running = False
        elif ev.type == pygame.KEYDOWN and ev.key == pygame.K_ESCAPE:
            running = False

    screen.fill(BG)

    # dot grid background
    for bx in range(0, W, 40):
        for by in range(0, H - BAR_H, 40):
            pygame.draw.circle(screen, (12,12,30), (bx, by), 1)

    # left/right divider
    pygame.draw.line(screen, BORDER, (LEFT_W, 0), (LEFT_W, H-BAR_H))

    # ── snapshot ──────────────────────────────────────────────
    with lock:
        snap    = {k: list(bufs[k]) for k in bufs}
        cur     = dict(latest)
        r_rad   = attitude['roll']
        p_rad   = attitude['pitch']
        y_rad   = attitude['yaw']
        is_conn = status['conn']
        cur_hz  = status['hz']

    # ── cube ──────────────────────────────────────────────────
    screen.blit(font_b.render("// 3D ORIENTATION", True, DIM), (10, 14))
    draw_cube(CUBE_CX, CUBE_CY, r_rad, p_rad, y_rad)
    bracket(CUBE_CX - CUBE_SC, CUBE_CY - CUBE_SC, CUBE_SC*2, CUBE_SC*2, BORDER)

    for i, (lbl, val, col) in enumerate([
        ("ROLL ", math.degrees(r_rad),        AX_COL),
        ("PITCH", math.degrees(p_rad),        AY_COL),
        ("YAW  ", math.degrees(y_rad) % 360,  AZ_COL),
    ]):
        yo = CUBE_CY + CUBE_SC + 16 + i*22
        screen.blit(font_b.render(lbl,             True, DIM), (20, yo))
        screen.blit(font_b.render(f"{val:+7.1f}°", True, col), (75, yo))

    sep_y = CUBE_CY + CUBE_SC + 84
    pygame.draw.line(screen, BORDER, (10, sep_y), (LEFT_W-10, sep_y))
    screen.blit(font_b.render("// ATTITUDE", True, DIM), (10, sep_y+8))

    draw_horizon(HOR_CX, HOR_CY, HOR_R, r_rad, p_rad)
    bracket(HOR_CX-HOR_R, HOR_CY-HOR_R, HOR_R*2, HOR_R*2, BORDER)

    # ── scopes ────────────────────────────────────────────────
    draw_scope(SX, SAY, SW, SH,
               [snap['ax'], snap['ay'], snap['az']],
               [AX_COL, AY_COL, AZ_COL],
               -20, 20, "ACCELEROMETER", "m/s²")
    bracket(SX, SAY, SW, SH, BORDER)
    legend([("AX",AX_COL),("AY",AY_COL),("AZ",AZ_COL)],
           SX + SW - 148, SAY + 20)

    draw_scope(SX, SGY, SW, SH,
               [snap['gx'], snap['gy'], snap['gz']],
               [GX_COL, GY_COL, GZ_COL],
               -10, 10, "GYROSCOPE", "rad/s")
    bracket(SX, SGY, SW, SH, BORDER)
    legend([("GX",GX_COL),("GY",GY_COL),("GZ",GZ_COL)],
           SX + SW - 148, SGY + 20)

    # ── overlays ──────────────────────────────────────────────
    screen.blit(_scanlines, (0, 0))
    screen.blit(_vignette,  (0, 0))

    # ── status bar ────────────────────────────────────────────
    bar_y = H - BAR_H
    pygame.draw.rect(screen, (6,6,18), (0, bar_y, W, BAR_H))
    pygame.draw.line(screen, BORDER,   (0, bar_y), (W, bar_y))

    mode_str = f"UDP :{UDP_PORT}" if UDP_MODE else f"SERIAL {PORT}"
    mode_col = (100, 180, 255) if UDP_MODE else (180, 255, 100)
    dot = (0,255,100) if is_conn else (255,50,50)
    pygame.draw.circle(screen, dot, (16, bar_y + BAR_H//2), 5)
    screen.blit(font_m.render(mode_str,        True, mode_col), (28,  bar_y+13))
    screen.blit(font_m.render("●" if is_conn else "○", True, dot), (28 + font_m.size(mode_str)[0] + 6, bar_y+13))
    screen.blit(font_m.render(f"{cur_hz:.0f} Hz", True, DIM), (190, bar_y+13))

    t_col = (255,80,80) if cur['temp'] > 40 else WHITE
    screen.blit(font_m.render(f"TEMP {cur['temp']:.1f}°C", True, t_col), (210, bar_y+13))

    for i, (txt, col) in enumerate([
        (f"AX {cur['ax']:+.2f}", AX_COL),
        (f"AY {cur['ay']:+.2f}", AY_COL),
        (f"AZ {cur['az']:+.2f}", AZ_COL),
        (f"GX {cur['gx']:+.3f}", GX_COL),
        (f"GY {cur['gy']:+.3f}", GY_COL),
        (f"GZ {cur['gz']:+.3f}", GZ_COL),
    ]):
        screen.blit(font_m.render(txt, True, col), (330 + i*155, bar_y+13))

    ram_mb = _proc.memory_info().rss / 1048576
    screen.blit(font_m.render(f"{ram_mb:.0f} MB", True, DIM), (W-130, bar_y+13))
    screen.blit(font_m.render(f"{clock.get_fps():.0f} FPS", True, DIM), (W-68,  bar_y+13))

    pygame.display.flip()
    clock.tick(60)

pygame.quit()
