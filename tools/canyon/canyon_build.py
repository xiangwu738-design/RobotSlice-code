"""夺芯峡谷地形：按灰盒规格（MechCoreArena::BuildCanyon）挖出可走区域，崖壁 / 崖顶起伏取自真实高程（马布尔峡谷，BlenderGIS 烘的 baked_dem.npz）。
做法：三维有符号距离场（实心 > 0）→ 表面网（surface nets）出网格，按 X 切 8 块，近景 25 厘米体素（视觉），50 厘米（碰撞）。
远景：同一套高度函数的高度场网格，中间挖掉近景范围。
py -3.11 -I canyon_build.py [--vox 25] [--tag vis] [--preview]
输出 Inbox/_canyon/mesh/<tag>_tileN.npz（verts 厘米 float32，tris int32）、far.npz、layout.json（散布用的表面信息）"""
import json, math, os, sys, time
import numpy as np
from scipy.ndimage import gaussian_filter, gaussian_filter1d, map_coordinates

ROOT = r"D:\RobotSlice\Inbox\_canyon"
OUT = os.path.join(ROOT, "mesh")
os.makedirs(OUT, exist_ok=True)
args = sys.argv[1:]
VOX = float(args[args.index("--vox") + 1]) if "--vox" in args else 25.0
TAG = args[args.index("--tag") + 1] if "--tag" in args else "vis"
PREVIEW = "--preview" in args

# ---------------------------------------------------------------- 灰盒规格（厘米）
LEN = 13000.0       # 两头端墙
FLOOR_W = 1800.0    # 谷底半宽
LEDGE_Z = 900.0
LEDGE_W = 2500.0
CAVE_Y0, CAVE_Y1, CAVE_H = 2700.0, 3700.0, 600.0
CAVE_X = 9000.0
OUTER_Y = 4600.0
ROCK_END = 12000.0
TOP_N, TOP_S = 2200.0, 2600.0
GATE_X = 4500.0
PORTALS = [(-500.0, 500.0), (-8400.0, -7600.0), (7600.0, 8400.0)]
FLAT_ZONES = [  # (x, y, r) 要绝对平的圆：回收站 A / B、出生点
    (8800, 1000, 650), (-8800, 1000, 650), (8200, -2150, 650), (-8200, -2150, 650)]
SPAWN_BOX = [(11150, 12850, -850, 850), (-12850, -11150, -850, 850)]
LIFT_BOX = [(6900, 7900, -1800, -900), (-7900, -6900, -1800, -900)]

# ---------------------------------------------------------------- 噪声
def _hash2(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return (h & 0xFFFF).astype(np.float32) / 32767.5 - 1.0


def vnoise2(x, y, seed):
    xi = np.floor(x); yi = np.floor(y)
    xf = (x - xi).astype(np.float32); yf = (y - yi).astype(np.float32)
    u = xf * xf * (3 - 2 * xf); v = yf * yf * (3 - 2 * yf)
    xi = xi.astype(np.int64); yi = yi.astype(np.int64)
    a = _hash2(xi, yi, seed); b = _hash2(xi + 1, yi, seed)
    c = _hash2(xi, yi + 1, seed); d = _hash2(xi + 1, yi + 1, seed)
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v


def fbm2(x, y, seed, octaves=4, gain=0.5, lac=2.03):
    s = 0.0; amp = 1.0; norm = 0.0
    for o in range(octaves):
        s = s + amp * vnoise2(x, y, seed + 131 * o)
        norm += amp; amp *= gain
        x = x * lac + 17.31; y = y * lac + 9.17
    return s / norm


def ridged2(x, y, seed, octaves=3):
    return 1.0 - np.abs(fbm2(x, y, seed, octaves))


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


rng = np.random.default_rng(20261029)

# ---------------------------------------------------------------- 真实高程（游戏坐标，已由 BlenderGIS 场景烘好）
dem = np.load(os.path.join(ROOT, "baked_dem.npz"))
Hn, nxs, nys = dem["near"].astype(np.float32), dem["nx"], dem["ny"]
Hf, fxs, fys = dem["far"].astype(np.float32), dem["fx"], dem["fy"]
Hn = gaussian_filter(Hn, 1.0)
# 拉直：以河道（最低处）中线为准；再从中线往两侧找崖缘（高过「该侧台地中位高度 - 2.5 米」）和坡脚（高过 1.7 米）
from scipy.ndimage import median_filter
_riv = np.array([nys[c < 80].mean() if (c < 80).any() else np.nan for c in Hn.T])
_riv = np.where(np.isnan(_riv), np.nanmedian(_riv), _riv)
cen = gaussian_filter1d(_riv, 20)                      # 河道中线（σ=10 米）
levN = float(np.median(Hn[nys > 4200])); levS = float(np.median(Hn[nys < -4200]))
rimN = np.empty(len(nxs)); rimS = np.empty(len(nxs)); toeN = np.empty(len(nxs)); toeS = np.empty(len(nxs))
for i in range(len(nxs)):
    col = Hn[:, i]; c = cen[i]
    up = nys > c; dn = nys < c
    yy = nys[up] - c; z = col[up]
    rimN[i] = yy[np.argmax(z > levN - 250.0)] if (z > levN - 250.0).any() else yy[-1]
    toeN[i] = yy[np.argmax(z > 170.0)]
    yy = c - nys[dn][::-1]; z = col[dn][::-1]
    rimS[i] = yy[np.argmax(z > levS - 250.0)] if (z > levS - 250.0).any() else yy[-1]
    toeS[i] = yy[np.argmax(z > 170.0)]
def _clean(v, lo, hi, sig):
    return gaussian_filter1d(median_filter(np.clip(v, lo, hi), 9), sig)
rimN_s = _clean(rimN, 2300, 4300, 3); rimS_s = _clean(rimS, 2300, 4300, 3)
toeN_s = _clean(toeN, 500, 2200, 5); toeS_s = _clean(toeS, 500, 2200, 5)
inside = np.abs(nxs) <= 12500


def to_recess(v, lo_pct, k, cap):
    base = np.percentile(v[inside], lo_pct)
    return np.clip((v - base) * k, 0.0, cap)


# 真实崖缘的进退 → 上层崖壁往外凹的量（只往外，不挤占栈道）
DEM_RECU_N = to_recess(rimN_s, 12, 0.45, 420.0)
DEM_RECU_S = to_recess(rimS_s, 12, 0.45, 420.0)
DEM_RECL_N = to_recess(toeN_s, 15, 0.12, 110.0)
DEM_RECL_S = to_recess(toeS_s, 15, 0.12, 110.0)
print(f"[dem] 台地高 北 {levN:.0f} 南 {levS:.0f}；崖缘 北 {rimN.min():.0f}~{rimN.max():.0f} 南 {rimS.min():.0f}~{rimS.max():.0f}；"
      f"中线偏移 {cen.min():.0f}~{cen.max():.0f}；上壁凹进 北 {DEM_RECU_N.mean():.0f} 南 {DEM_RECU_S.mean():.0f}（均值，厘米）")


def sample_near(x, y):
    """拉直后的近景真实高度（x、y 游戏厘米，可广播）"""
    c = np.interp(x, nxs, cen)
    fi = (x - nxs[0]) / (nxs[1] - nxs[0]); fj = (y + c - nys[0]) / (nys[1] - nys[0])
    fi, fj = np.broadcast_arrays(fi, fj)
    return map_coordinates(Hn, [fj.ravel(), fi.ravel()], order=1, mode="nearest").reshape(fi.shape)


Hf_filled = Hf.copy()
nanm = np.isnan(Hf_filled)
if nanm.any():
    from scipy.ndimage import distance_transform_edt
    idx = distance_transform_edt(nanm, return_distances=False, return_indices=True)
    Hf_filled = Hf_filled[tuple(idx)]
Hf_filled = gaussian_filter(Hf_filled, 0.8)


def sample_far(x, y):
    c = np.interp(np.clip(x, nxs[0], nxs[-1]), nxs, cen)
    fi = (x - fxs[0]) / (fxs[1] - fxs[0]); fj = (y + c - fys[0]) / (fys[1] - fys[0])
    fi, fj = np.broadcast_arrays(fi, fj)
    return map_coordinates(Hf_filled, [fj.ravel(), fi.ravel()], order=1, mode="nearest").reshape(fi.shape)


# ---------------------------------------------------------------- 高度函数（x、y 可广播）
def plateau_h(x, y):
    """内台地（北 22 米 / 南 26 米）：真实台地在崖缘后的起伏，靠崖边收小"""
    ay = np.abs(y)
    north = y > 0
    rim_here = np.where(north, np.interp(x, nxs, rimN_s), np.interp(x, nxs, rimS_s))
    yy = np.where(north, 1.0, -1.0) * (rim_here + 300.0 + np.maximum(ay - LEDGE_W, 0.0) * 0.9)
    real = sample_near(x, yy)
    lev = np.where(north, levN, levS)
    rel = np.clip(real - lev, -160.0, 380.0)
    taper = 0.35 + 0.65 * smoothstep(LEDGE_W, LEDGE_W + 900.0, ay)
    fine = fbm2(x / 900.0, y / 900.0, 11, 4) * 35.0 + fbm2(x / 180.0, y / 180.0, 12, 3) * 8.0
    base = np.where(north, TOP_N, TOP_S)
    return base + rel * 0.75 * taper + fine


def outer_h(x, y):
    """外圈高原（围住峡谷，45 米以上），远处是真实的回声 / 朱红崖（Echo / Vermilion Cliffs）"""
    real = sample_far(x, y)
    rel = np.maximum(real - levN, 0.0)
    d = np.maximum(np.maximum(np.abs(x) - LEN, 0.0), np.maximum(np.abs(y) - OUTER_Y, 0.0))
    ramp = smoothstep(800.0, 9000.0, d)
    fine = fbm2(x / 1400.0, y / 1400.0, 21, 5) * 140.0 + fbm2(x / 260.0, y / 260.0, 22, 3) * 20.0
    return 4700.0 + rel * (0.25 + 0.75 * ramp) + fine + ramp * 600.0


def floor_h(x, y):
    """谷底：±6 厘米的沙面起伏，回收站 / 出生点 / 吊笼处压平"""
    b = fbm2(x / 700.0, y / 700.0, 31, 3) * 6.0
    riv = 1.0 - smoothstep(480.0, 680.0, np.abs(y))          # 河床比水面（+2 厘米）低一点，免得穿出水面
    b = b * (1.0 - riv) + (-np.abs(b) - 2.5) * riv
    flat = np.ones(np.broadcast(x, y).shape, np.float32)
    for (cx, cy, r) in FLAT_ZONES:
        flat = np.minimum(flat, smoothstep(r, r + 400.0, np.hypot(x - cx, y - cy)))
    for (x0, x1, y0, y1) in SPAWN_BOX + LIFT_BOX:
        dx = np.maximum(np.maximum(x0 - x, x - x1), 0.0); dy = np.maximum(np.maximum(y0 - y, y - y1), 0.0)
        flat = np.minimum(flat, smoothstep(0.0, 400.0, np.hypot(dx, dy)))
    return b * flat


def ledge_h(x, y):
    b = fbm2(x / 500.0, y / 500.0, 41, 3) * 14.0 + fbm2(x / 120.0, y / 120.0, 42, 2) * 3.0
    flat = np.ones(np.broadcast(x, y).shape, np.float32)
    for (cx, cy, r) in FLAT_ZONES[2:]:
        flat = np.minimum(flat, smoothstep(r, r + 400.0, np.hypot(x - cx, y - cy)))
    for s in (-1, 1):  # 吊笼上沿、坡道顶接栈道处
        flat = np.minimum(flat, smoothstep(300.0, 700.0, np.hypot(x - s * 7400.0, np.minimum(np.abs(y + 1350.0), np.abs(y + 2000.0)))))
        flat = np.minimum(flat, smoothstep(300.0, 700.0, np.hypot(x - s * 3700.0, np.abs(y) - 1800.0)))
    return LEDGE_Z + b * flat


# ---------------------------------------------------------------- 崖壁凹进（只往外，单位厘米）
def strata(z, seed, scale=1.0):
    """水平层理：每层 40–220 厘米，软层凹进去，硬层突出（一维表，按 z 查）"""
    r = np.random.default_rng(seed)
    zs = np.arange(-200.0, 7000.0, 5.0)
    prof = np.zeros_like(zs)
    zc = -200.0
    while zc < 7000.0:
        th = r.uniform(40.0, 220.0)
        off = r.choice([0.0, r.uniform(5, 18), r.uniform(18, 45)], p=[0.35, 0.4, 0.25])
        prof[(zs >= zc) & (zs < zc + th)] = off
        zc += th
    prof = gaussian_filter1d(prof, 1.6)
    return np.interp(z, zs, prof) * scale


def joints(xline, seed, depth=(25, 90), width=(40, 140), spacing=(160, 650)):
    """竖向节理：沿墙面方向的一维凹槽"""
    r = np.random.default_rng(seed)
    out = np.zeros_like(xline, dtype=np.float32)
    x = xline.min() - 500.0
    while x < xline.max() + 500.0:
        x += r.uniform(*spacing)
        w = r.uniform(*width); d = r.uniform(*depth)
        t = np.clip(1.0 - np.abs(xline - x) / w, 0.0, 1.0)
        out = np.maximum(out, d * t * t)
    return out


def voronoi_facets(U, Z, seed, su, sz, depth, tilt=0.10, crack=30.0, crack_w=0.10):
    """各向异性 Voronoi 破碎块：每块一个随机凹进量 + 小倾斜面，块缝处加裂缝。U、Z 是（厘米）二维广播数组"""
    u = U / su; z = Z / sz
    shape = np.broadcast(u, z).shape
    u = np.broadcast_to(u, shape); z = np.broadcast_to(z, shape)
    iu = np.floor(u).astype(np.int64); iz = np.floor(z).astype(np.int64)
    best = np.full(shape, 1e9, np.float32); second = best.copy()
    bu = np.zeros(shape, np.int64); bz = np.zeros(shape, np.int64)
    bpu = np.zeros(shape, np.float32); bpz = np.zeros(shape, np.float32)
    for du in (-1, 0, 1):
        for dz in (-1, 0, 1):
            cu = iu + du; cz = iz + dz
            pu = cu + 0.5 + _hash2(cu, cz, seed) * 0.42
            pz = cz + 0.5 + _hash2(cu, cz, seed + 1) * 0.42
            d = (u - pu) ** 2 + (z - pz) ** 2
            closer = d < best
            second = np.where(closer, best, np.minimum(second, d))
            best = np.where(closer, d, best)
            bu = np.where(closer, cu, bu); bz = np.where(closer, cz, bz)
            bpu = np.where(closer, pu, bpu); bpz = np.where(closer, pz, bpz)
    off = (_hash2(bu, bz, seed + 2) + 1.0) * 0.5 * depth
    tu = _hash2(bu, bz, seed + 3) * tilt * su; tz = _hash2(bu, bz, seed + 4) * tilt * sz
    rec = off + tu * (u - bpu) + tz * (z - bpz)
    edge = np.sqrt(second) - np.sqrt(best)
    rec = rec + crack * (1.0 - smoothstep(0.0, crack_w, edge))
    return np.maximum(rec, 0.0)


def strata_major(z, seed, n_bands, amp):
    """少数几条明显的层理台阶（软层凹进）"""
    r = np.random.default_rng(seed)
    out = np.zeros(np.shape(z), np.float32)
    for _ in range(n_bands):
        zc = r.uniform(-200, 7000); th = r.uniform(30, 120); a = r.uniform(0.4, 1.0) * amp
        out = out + a * smoothstep(zc - 12, zc + 12, z) * (1 - smoothstep(zc + th - 12, zc + th + 12, z))
    return out


def wall_recess(u, z, seed, dem_rec=None, z0=0.0, z1=900.0, top_round=0.0, undercut=0.0, lip=0.0, batter=0.03, scale=1.0):
    """u：沿墙方向坐标（1D），z：高度（1D）→ (len(u), len(z)) 往外凹进量（厘米，≥0）
    低频起伏 + 两层 Voronoi 破碎块 + 少量层理台阶 + 细噪声；lip>0 时顶边收回成硬帽岩（栈道边沿不缩）"""
    U = u[:, None].astype(np.float32); Z = z[None, :].astype(np.float32)
    zw = Z + fbm2(U / 2600.0, np.zeros_like(U), seed + 1, 2) * 60.0       # 层理微倾、块缝不水平
    uw = U + fbm2(U / 700.0, Z / 500.0, seed + 9, 2) * 120.0               # 块边不竖直
    low = (fbm2(U / 1800.0, Z / 1400.0, seed + 6, 3) + 1.0) * 0.5 * 110.0
    big = voronoi_facets(uw, zw, seed + 10, 900.0 * scale, 520.0 * scale, 95.0, tilt=0.10, crack=35.0, crack_w=0.08)
    mid = voronoi_facets(uw + 333.0, zw, seed + 20, 260.0 * scale, 170.0 * scale, 28.0, tilt=0.14, crack=12.0, crack_w=0.10)
    st = strata_major(zw, seed + 2, 14, 32.0) * (0.4 + 0.6 * (fbm2(U / 1500.0, Z / 900.0, seed + 3, 2) + 1.0) * 0.5)
    small = (fbm2(U / 90.0, Z / 90.0, seed + 7, 3) + 1.0) * 0.5 * 9.0
    rec = low + big + mid + st + small + batter * np.maximum(Z - z0, 0.0)
    if dem_rec is not None:
        rec = rec + dem_rec[:, None]
    if undercut > 0:
        uc = undercut * (0.3 + 0.7 * (fbm2(U / 1300.0, np.zeros_like(U), seed + 8, 2) + 1.0) * 0.5)
        rec = rec + uc * smoothstep(z0 + 80.0, z0 + 320.0, Z) * (1.0 - smoothstep(z1 - 330.0, z1 - 110.0, Z))
    if lip > 0:
        k = smoothstep(z1 - 170.0, z1 - 45.0, Z)
        cap = (fbm2(U / 220.0, np.zeros_like(U), seed + 11, 3) + 1.0) * 0.5 * lip
        rec = rec * (1.0 - k) + cap * k
    if top_round > 0:
        rec = rec + top_round * smoothstep(z1 - 280.0, z1 + 40.0, Z) ** 2
    return rec.astype(np.float32)


# ---------------------------------------------------------------- 网格范围
X0, X1 = -14400.0, 14400.0
Y0, Y1 = -5600.0, 5600.0
Z0 = -100.0
xs = np.arange(X0 + VOX / 2, X1, VOX)
ys = np.arange(Y0 + VOX / 2, Y1, VOX)
# 上界：外圈高原最高处
_hx = np.linspace(X0, X1, 300)[:, None]; _hy = np.linspace(Y0, Y1, 120)[None, :]
ZTOP = float(np.max(outer_h(_hx, _hy))) + 250.0
zs = np.arange(Z0 + VOX / 2, ZTOP, VOX)
NX, NY, NZ = len(xs), len(ys), len(zs)
print(f"[grid] 体素 {VOX:.0f} 厘米：{NX} x {NY} x {NZ} = {NX * NY * NZ / 1e6:.0f} M，Z 到 {ZTOP:.0f}")

# 一维 / 二维场（整域预算）
DEMRU_N = np.interp(xs, nxs, DEM_RECU_N); DEMRU_S = np.interp(xs, nxs, DEM_RECU_S)
DEMRL_N = np.interp(xs, nxs, DEM_RECL_N); DEMRL_S = np.interp(xs, nxs, DEM_RECL_S)
near_lift = np.clip(1.0 - smoothstep(500.0, 900.0, np.abs(np.abs(xs) - 7400.0)), 0, 1)
REC_L_N = wall_recess(xs, zs, 100, DEMRL_N, 0.0, LEDGE_Z, undercut=120.0, lip=28.0, batter=0.0, scale=0.8)
REC_L_S = wall_recess(xs, zs, 200, DEMRL_S, 0.0, LEDGE_Z, undercut=120.0, lip=28.0, batter=0.0, scale=0.8)
REC_L_S *= (1.0 - 0.85 * near_lift)[:, None]           # 吊笼井那面墙收直
REC_U_N = wall_recess(xs, zs, 300, DEMRU_N, LEDGE_Z, TOP_N, top_round=170.0, batter=0.05)
REC_U_S = wall_recess(xs, zs, 400, DEMRU_S, LEDGE_Z, TOP_S, top_round=170.0, batter=0.05)
REC_O_N = wall_recess(xs, zs, 500, None, TOP_N, 4700.0, top_round=240.0, batter=0.08, scale=1.5) + \
    ((fbm2(xs / 2500.0, np.zeros_like(xs), 51, 3) + 1.0) * 0.5 * 320.0)[:, None]
REC_O_S = wall_recess(xs, zs, 600, None, TOP_S, 4700.0, top_round=240.0, batter=0.08, scale=1.5) + \
    ((fbm2(xs / 2500.0, np.ones_like(xs), 52, 3) + 1.0) * 0.5 * 320.0)[:, None]
# 两头端墙（x=±13000，沿 y），栈道端墙（x=±12000）
REC_E_W = wall_recess(ys, zs, 700, None, 0.0, 4700.0, top_round=240.0, batter=0.04, scale=1.3) + 40.0
REC_E_E = wall_recess(ys, zs, 800, None, 0.0, 4700.0, top_round=240.0, batter=0.04, scale=1.3) + 40.0
REC_LE_W = wall_recess(ys, zs, 900, None, LEDGE_Z, TOP_S, batter=0.03) * 0.6
REC_LE_E = wall_recess(ys, zs, 950, None, LEDGE_Z, TOP_S, batter=0.03) * 0.6
# 岩洞：内侧墙（y=2700，往谷里凹）、外侧墙（y=3700，往外凹）、洞顶（往上凹，拱形）
gate_w = smoothstep(120.0, 420.0, np.abs(np.abs(xs) - GATE_X))   # 闸门处 = 0（洞截面严格等于规格，再往里收 15 厘米）
REC_CI = wall_recess(xs, zs, 1000, None, 0.0, CAVE_H, batter=0.0, scale=0.6) * 0.45
REC_CO = wall_recess(xs, zs, 1100, None, 0.0, CAVE_H, batter=0.0, scale=0.6) * 0.6
REC_CI = REC_CI * gate_w[:, None] - 15.0 * (1 - gate_w[:, None])
REC_CO = REC_CO * gate_w[:, None] - 15.0 * (1 - gate_w[:, None])
cave_yy = np.clip((ys - (CAVE_Y0 + CAVE_Y1) * 0.5) / ((CAVE_Y1 - CAVE_Y0) * 0.5), -1.2, 1.2)
CEIL_C = (CAVE_H + (np.maximum(0, 1 - cave_yy[None, :] ** 2) * 150.0 * (0.5 + 0.5 * (fbm2(xs[:, None] / 900.0, cave_yy[None, :], 61, 2) + 1) * 0.5)
          + (fbm2(xs[:, None] / 200.0, ys[None, :] / 200.0, 62, 3) + 1) * 0.5 * 30.0) * gate_w[:, None] - 15.0 * (1 - gate_w[:, None])).astype(np.float32)
CAVE_END = (fbm2(ys / 300.0, np.zeros_like(ys), 63, 3) + 1) * 0.5 * 120.0
# 洞口：两侧往外扩、顶往上扩
PORT_SIDE = (fbm2(zs / 150.0, np.zeros_like(zs), 64, 3) + 1) * 0.5 * 70.0
PORT_TOP = (fbm2(xs / 150.0, np.zeros_like(xs), 65, 3) + 1) * 0.5 * 45.0

FLOOR = floor_h(xs[:, None], ys[None, :]).astype(np.float32)
LEDGE = ledge_h(xs[:, None], ys[None, :]).astype(np.float32)
HP = plateau_h(xs[:, None], ys[None, :]).astype(np.float32)
HO = outer_h(xs[:, None], ys[None, :]).astype(np.float32)
print(f"[fields] 内台地 {HP[:, np.abs(ys) > 2600].min():.0f}~{HP.max():.0f}，外圈 {HO.min():.0f}~{HO.max():.0f}")


# ---------------------------------------------------------------- 距离场（一块 X 区间）
def density(i0, i1):
    """返回 D[i0:i1, :, :]（实心 > 0）"""
    x = xs[i0:i1][:, None, None]; y = ys[None, :, None]; z = zs[None, None, :]
    ax = np.abs(x); ay = np.abs(y)
    north = y > 0
    sl = slice(i0, i1)
    recL = np.where(north, REC_L_N[sl][:, None, :], REC_L_S[sl][:, None, :])
    recU = np.where(north, REC_U_N[sl][:, None, :], REC_U_S[sl][:, None, :])
    recO = np.where(north, REC_O_N[sl][:, None, :], REC_O_S[sl][:, None, :])
    recE = np.where(x > 0, REC_E_E[None, :, :], REC_E_W[None, :, :])
    recLE = np.where(x > 0, REC_LE_E[None, :, :], REC_LE_W[None, :, :])
    fl = FLOOR[sl][:, :, None]; le = LEDGE[sl][:, :, None]
    hp = HP[sl][:, :, None]; ho = HO[sl][:, :, None]

    # 空腔（> 0 是空气）
    a_floor = np.minimum(np.minimum(z - fl, (FLOOR_W + recL) - ay), (LEN + recE) - ax)
    a_ledge = np.minimum(np.minimum(z - le, (LEDGE_W + recU) - ay), (ROCK_END + recLE) - ax)
    a_plat = np.minimum(np.minimum(z - hp, (OUTER_Y + recO) - ay), (LEN + recE) - ax)
    air = np.maximum(np.maximum(a_floor, a_ledge), a_plat)
    del a_floor, a_ledge, a_plat
    # 岩洞
    rci = REC_CI[sl][:, None, :]; rco = REC_CO[sl][:, None, :]
    ceil = CEIL_C[sl][:, :, None]
    cend = np.interp(ys, ys, CAVE_END)[None, :, None]
    a_cave = np.minimum(np.minimum(np.minimum(z - 0.0, ceil - z), np.minimum(y - (CAVE_Y0 - rci), (CAVE_Y1 + rco) - y)), (CAVE_X + cend) - ax)
    air = np.maximum(air, a_cave)
    del a_cave
    # 洞口
    ps = np.interp(zs, zs, PORT_SIDE)[None, None, :]
    pt = PORT_TOP[sl][:, None, None]
    for (xa, xb) in PORTALS:
        xc, hw = (xa + xb) * 0.5, (xb - xa) * 0.5
        bulge = 130.0 * np.clip(1.0 - ((x - xc) / (hw + 60.0)) ** 2, 0.0, 1.0) * smoothstep(2650.0, 2000.0, y)   # 洞口顶拱起（外口高、往里收）
        flare = 70.0 * smoothstep(2350.0, 1800.0, y)                                                             # 外口往两边张开
        a_p = np.minimum(np.minimum(np.minimum(z - 0.0, (CAVE_H + pt + bulge) - z), np.minimum(y - 1650.0, 2800.0 - y)),
                         np.minimum(x - (xa - ps - flare), (xb + ps + flare) - x))
        air = np.maximum(air, a_p)
    D = np.minimum(ho - z, -air)
    del air
    # 坡道（实心楔形，贴崖壁）
    rz = LEDGE_Z * (ax - 1300.0) / 2400.0
    rside = (fbm2(x[:, :, 0] / 160.0, np.full_like(x[:, :, 0], 3.0), 81, 3)[:, :, None] + 1.0) * 0.5 * 45.0 * smoothstep(rz - 40.0, rz - 160.0, z)
    ramp = np.minimum(np.minimum(rz - z, ay - (1200.0 + rside)), np.minimum(np.minimum(ax - 1300.0, 3700.0 - ax), 2150.0 - ay))
    D = np.maximum(D, ramp)
    # 中央：天然石拱（桥面 Z 900，X ±300）+ 铸造台顶（Z 700–800，±600）+ 四根石柱
    if i0 < np.searchsorted(xs, 1000.0) and i1 > np.searchsorted(xs, -1000.0):
        under = np.where(ay <= 600.0, 800.0, 800.0 - 200.0 * np.clip((ay - 600.0) / 1200.0, 0, 1.2) ** 1.8)
        under = under + fbm2(x[:, :, 0] / 150.0, y[:, :, 0] / 150.0, 71, 3)[:, :, None] * 18.0
        side = 300.0 + 450.0 * smoothstep(1000.0, 1800.0, ay) * smoothstep(870.0, 480.0, z) - \
            np.clip(fbm2(y[:, :, 0] / 130.0, np.zeros_like(y[:, :, 0]), 72, 3)[:, :, None] * 30.0, 0, 30) * smoothstep(870.0, 820.0, z)
        arch = np.minimum(np.minimum(LEDGE_Z - z, z - under), np.minimum(side - ax, 2100.0 - ay))
        D = np.maximum(D, arch)
        slab = np.minimum(np.minimum(800.0 - z, z - 700.0), np.minimum(600.0 - ax, 600.0 - ay))
        D = np.maximum(D, slab)
        for sx_ in (-450.0, 450.0):
            for sy_ in (-450.0, 450.0):
                pr = 150.0 - np.clip(fbm2((x[:, :, 0] + y[:, :, 0]) / 90.0, np.zeros_like(x[:, :, 0]), 73, 2)[:, :, None] + 0.5, 0, 1) * 18.0 \
                    - (fbm2(z[0] / 120.0 + sx_, z[0] * 0 + sy_, 74, 2) + 1) * 6.0
                ph = np.mod(z[0] + 30.0, 150.0)                                     # 一节节叠起来的石鼓（古代石柱）
                drum = (_hash2(np.floor((z[0] + 30.0) / 150.0).astype(np.int64), np.full(z[0].shape, int(sx_ + 3 * sy_), np.int64), 75) + 1.0) * 5.0
                pr = pr - drum - 11.0 * smoothstep(14.0, 0.0, np.minimum(ph, 150.0 - ph))
                pil =np.minimum(np.minimum(pr - np.abs(x - sx_), pr - np.abs(y - sy_)), np.minimum(z + 50.0, 760.0 - z))
                D = np.maximum(D, pil)
    return D.astype(np.float32)


# ---------------------------------------------------------------- 表面网
OFFS = np.array([(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0), (0, 0, 1), (1, 0, 1), (0, 1, 1), (1, 1, 1)])
EDGES = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]


def surface_nets(D, origin, own_x_edges, own_yz_lo):
    """D：(L+1, NY, NZ)。返回 verts（厘米）、tris。
    own_x_edges：本块负责的 x 向边（本地 i 范围），own_yz_lo：本块负责的 y/z 向边的最小本地点列（0 或 1）"""
    S = D > 0
    L1, ny, nz = D.shape
    cs = [S[OFFS[k, 0]:L1 - 1 + OFFS[k, 0], OFFS[k, 1]:ny - 1 + OFFS[k, 1], OFFS[k, 2]:nz - 1 + OFFS[k, 2]] for k in range(8)]
    anyS = cs[0].copy(); allS = cs[0].copy()
    for c in cs[1:]:
        anyS |= c; allS &= c
    active = anyS & ~allS
    del cs, anyS, allS
    ci, cj, ck = np.nonzero(active)
    n = len(ci)
    vals = np.stack([D[ci + o[0], cj + o[1], ck + o[2]] for o in OFFS], 1)
    acc = np.zeros((n, 3), np.float32); cnt = np.zeros(n, np.float32)
    for a, b in EDGES:
        va, vb = vals[:, a], vals[:, b]
        m = (va > 0) != (vb > 0)
        t = np.where(m, va / np.where(m, va - vb, 1.0), 0.0)
        p = OFFS[a][None, :] + t[:, None] * (OFFS[b] - OFFS[a])[None, :]
        acc[m] += p[m]; cnt[m] += 1
    pos = (np.stack([ci, cj, ck], 1) + acc / cnt[:, None]) * VOX + origin[None, :]
    vid = np.full(active.shape, -1, np.int32)
    vid[ci, cj, ck] = np.arange(n, dtype=np.int32)
    del active
    quads = []; normals = []
    # x 向边：点 (i,j,k)-(i+1,j,k)，相邻格 (i, j-1..j, k-1..k)
    i_lo, i_hi = own_x_edges
    e = S[i_lo:i_hi, 1:-1, 1:-1] != S[i_lo + 1:i_hi + 1, 1:-1, 1:-1]
    ii, jj, kk = np.nonzero(e); ii += i_lo; jj += 1; kk += 1
    sgn = np.where(S[ii, jj, kk], 1, -1)
    q = np.stack([vid[ii, jj - 1, kk - 1], vid[ii, jj, kk - 1], vid[ii, jj, kk], vid[ii, jj - 1, kk]], 1)
    quads.append(q); normals.append(np.stack([sgn, 0 * sgn, 0 * sgn], 1))
    # y 向边：点 (i,j,k)-(i,j+1,k)，相邻格 (i-1..i, j, k-1..k)
    e = S[own_yz_lo:L1 - 1, :-1, 1:-1] != S[own_yz_lo:L1 - 1, 1:, 1:-1]
    ii, jj, kk = np.nonzero(e); ii += own_yz_lo; kk += 1
    sgn = np.where(S[ii, jj, kk], 1, -1)
    q = np.stack([vid[ii - 1, jj, kk - 1], vid[ii, jj, kk - 1], vid[ii, jj, kk], vid[ii - 1, jj, kk]], 1)
    quads.append(q); normals.append(np.stack([0 * sgn, sgn, 0 * sgn], 1))
    # z 向边：点 (i,j,k)-(i,j,k+1)，相邻格 (i-1..i, j-1..j, k)
    e = S[own_yz_lo:L1 - 1, 1:-1, :-1] != S[own_yz_lo:L1 - 1, 1:-1, 1:]
    ii, jj, kk = np.nonzero(e); ii += own_yz_lo; jj += 1
    sgn = np.where(S[ii, jj, kk], 1, -1)
    q = np.stack([vid[ii - 1, jj - 1, kk], vid[ii, jj - 1, kk], vid[ii, jj, kk], vid[ii - 1, jj, kk]], 1)
    quads.append(q); normals.append(np.stack([0 * sgn, 0 * sgn, sgn], 1))
    Q = np.concatenate(quads); NN = np.concatenate(normals).astype(np.float32)
    ok = (Q >= 0).all(1)
    Q, NN = Q[ok], NN[ok]
    # 按短对角线切三角，按期望法线定朝向
    p0, p1, p2, p3 = pos[Q[:, 0]], pos[Q[:, 1]], pos[Q[:, 2]], pos[Q[:, 3]]
    d02 = ((p0 - p2) ** 2).sum(1); d13 = ((p1 - p3) ** 2).sum(1)
    use02 = d02 <= d13
    T1 = np.where(use02[:, None], Q[:, [0, 1, 2]], Q[:, [0, 1, 3]])
    T2 = np.where(use02[:, None], Q[:, [0, 2, 3]], Q[:, [1, 2, 3]])
    T = np.concatenate([T1, T2]); TN = np.concatenate([NN, NN])
    a, b, c = pos[T[:, 0]], pos[T[:, 1]], pos[T[:, 2]]
    fn = np.cross(b - a, c - a)
    flip = (fn * TN).sum(1) < 0
    T[flip] = T[flip][:, [0, 2, 1]]
    # 去掉没用到的顶点
    used = np.zeros(len(pos), bool); used[T.ravel()] = True
    remap = np.cumsum(used) - 1
    return pos[used].astype(np.float32), remap[T].astype(np.int32)


def build_tiles(tile_w=3600.0):
    edges_x = np.arange(X0, X1 + 1, tile_w)
    tiles = []
    for t in range(len(edges_x) - 1):
        c0 = int(np.searchsorted(xs, edges_x[t]))           # 本块第一个格（点 c0 到 c0+1）
        c1 = int(np.searchsorted(xs, edges_x[t + 1]))       # 下一块的第一个格
        c1 = min(c1, NX - 1)
        p_hi = min(c1 + 2, NX)                              # 需要的点：c0 .. c1+1（含）
        t0 = time.time()
        D = density(c0, p_hi)
        origin = np.array([xs[c0], ys[0], zs[0]], np.float32)
        L = p_hi - c0
        own_x = (0, min(c1 - c0, L - 1))
        own_yz_lo = 1
        v, f = surface_nets(D, origin, own_x, own_yz_lo)
        del D
        np.savez_compressed(os.path.join(OUT, f"{TAG}_tile{t}.npz"), verts=v, tris=f)
        tiles.append(dict(tile=t, x0=float(edges_x[t]), x1=float(edges_x[t + 1]), verts=int(len(v)), tris=int(len(f))))
        print(f"[tile {t}] x {edges_x[t]:.0f}~{edges_x[t + 1]:.0f}：顶点 {len(v)}，三角 {len(f)}，{time.time() - t0:.1f}s", flush=True)
    return tiles


def build_far():
    """远景高度场（2.5 米 → 40 米渐变网格），挖掉近景范围（留 4 米重叠）"""
    R = 160000.0
    def axis(c_in, step_in, step_out, r):
        a = [0.0]; s = 0.0
        while s < r:
            st = step_in if s < c_in else min(step_out, step_in + (s - c_in) * 0.06)
            s += st; a.append(s)
        a = np.array(a)
        return np.concatenate([-a[:0:-1], a])
    gx = axis(14400.0, 250.0, 4000.0, R); gy = axis(5600.0, 250.0, 4000.0, R)
    H = outer_h(gx[:, None], gy[None, :]).astype(np.float32)
    dcore = np.maximum(np.abs(gx)[:, None] - 14400.0, np.abs(gy)[None, :] - 5600.0)
    H -= 20.0 * (1.0 - smoothstep(0.0, 1500.0, dcore))       # 和近景重叠处压低 20 厘米，免得闪
    nx_, ny_ = len(gx), len(gy)
    X, Y = np.meshgrid(gx, gy, indexing="ij")
    verts = np.stack([X.ravel(), Y.ravel(), H.ravel()], 1).astype(np.float32)
    ii, jj = np.meshgrid(np.arange(nx_ - 1), np.arange(ny_ - 1), indexing="ij")
    cx = 0.5 * (gx[ii] + gx[ii + 1]); cy = 0.5 * (gy[jj] + gy[jj + 1])
    keep = ~((np.abs(cx) < 14000.0) & (np.abs(cy) < 5200.0))
    ii, jj = ii[keep], jj[keep]
    a = ii * ny_ + jj; b = (ii + 1) * ny_ + jj; c = (ii + 1) * ny_ + jj + 1; d = ii * ny_ + jj + 1
    tris = np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)]).astype(np.int32)
    used = np.zeros(len(verts), bool); used[tris.ravel()] = True
    remap = np.cumsum(used) - 1
    np.savez_compressed(os.path.join(OUT, "far.npz"), verts=verts[used], tris=remap[tris].astype(np.int32))
    print(f"[far] 网格 {nx_}x{ny_}，三角 {len(tris)}，高 {H.min():.0f}~{H.max():.0f}")


def preview():
    """俯视高度 + 几个横截面（谷底 / 栈道 / 岩洞 / 闸门 / 洞口），检查规格"""
    from PIL import Image, ImageDraw
    secs = [0.0, -3000.0, 4500.0, 8000.0, -10000.0, 12500.0]
    W, Hh = 900, 520
    sheet = Image.new("RGB", (W * 2, Hh * 3), (20, 20, 24))
    for n, xv in enumerate(secs):
        i = int(np.argmin(np.abs(xs - xv)))
        D = density(i, i + 1)[0]  # (NY, NZ)
        img = np.where(D.T[::-1] > 0, 0.55, 0.12)
        im = Image.fromarray((np.stack([img * 1.1, img * 0.85, img * 0.7], -1) * 255).clip(0, 255).astype(np.uint8)).resize((W, int(W * NZ / NY)), Image.NEAREST)
        dr = ImageDraw.Draw(im)
        sx_ = W / (Y1 - Y0); sz_ = im.size[1] / (ZTOP - Z0)
        def P(yv, zv): return ((yv - Y0) * sx_, im.size[1] - (zv - Z0) * sz_)
        for (ya, yb, za, zb) in [(-1800, 1800, 0, 0), (1800, 2500, 900, 900), (-2500, -1800, 900, 900), (2500, 4600, 2200, 2200), (-4600, -2500, 2600, 2600)]:
            dr.line([P(ya, za), P(yb, zb)], fill=(0, 255, 255), width=1)
        for (yv, za, zb) in [(1800, 0, 900), (-1800, 0, 900), (2500, 900, 2200), (-2500, 900, 2600), (4600, 2200, 4500), (-4600, 2600, 4500)]:
            dr.line([P(yv, za), P(yv, zb)], fill=(255, 80, 80), width=1)
        if abs(xv) < 9000:
            dr.rectangle([P(2700, 600), P(3700, 0)], outline=(255, 255, 0))
        dr.text((8, 8), f"X={xv:.0f}", fill=(255, 255, 255))
        crop = im.crop((0, max(0, im.size[1] - Hh), W, im.size[1]))
        sheet.paste(crop, ((n % 2) * W, (n // 2) * Hh))
    sheet.save(os.path.join(OUT, f"sections_{TAG}.png"))
    print("[preview] 横截面", os.path.join(OUT, f"sections_{TAG}.png"))


if __name__ == "__main__":
    t0 = time.time()
    if PREVIEW:
        preview()
    else:
        tiles = build_tiles()
        if TAG == "vis":
            build_far()
        json.dump(dict(vox=VOX, tiles=tiles, ztop=ZTOP, levN=levN, levS=levS), open(os.path.join(OUT, f"{TAG}_layout.json"), "w"), indent=1)
    print(f"[done] {time.time() - t0:.0f}s")
