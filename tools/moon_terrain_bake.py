"""月球基地：NASA Apollo 17 高程图 → UE 地形。先跑 moon_layout.py。
py -3 -I tools/moon_terrain_bake.py [--preview]
输入：Inbox/_moon_nasa/dtm/NAC_DTM_APOLLO17_2M.TIF（2 米/像素）、Inbox/_k65/layout.json
输出（Inbox/_moon_nasa/work/）：
- near_height.png：1009×1009 16 位高度图（2 米一格，2016 米见方），Landscape 用
- near.npy：同一份高度（米，UE Z）；far.npz：远景网格（16 米一格、高度米）
- layout_final.json：布局 + 每件的 Z、出生点 Z、石头、Landscape 的位置 / Z 缩放
处理：
1. 只放大高度（KZ 倍），水平 1:1；基准 = 基地范围的中位高度（Z=0）
2. 基地范围（约 500×380 米）去掉低频起伏（东边比西边高，不公平），保留小起伏 ×0.7；再往外渐变回真实地形
3. 建筑占地压平（Pad，外扩 + 平滑过渡）；KEEP 里的环形坑不动
4. 真实数据里的无效值：选的这块没有；远景用最近邻填，只有内部小洞才填
"""
import json
import math
import random
import sys

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

sys.path.insert(0, 'D:/RobotSlice/tools')
from moon_dtm_survey import open_dtm  # noqa: E402

W = 'D:/RobotSlice/Inbox/_moon_nasa/work'
KZ = 2.0                     # 高度放大倍数
CX_KM, CY_KM = 9.964, 33.212  # 原点（大穹顶中心）在高程图上的位置
N = 1009
STEP = 2.0                   # 米 / 格
L = json.load(open('D:/RobotSlice/Inbox/_k65/layout.json', encoding='utf-8'))

dtm, _ = open_dtm()
r0 = int(round(CY_KM * 500)) - N // 2
c0 = int(round(CX_KM * 500)) - N // 2
raw = np.array(dtm[r0:r0 + N, c0:c0 + N], dtype=np.float64)
bad = raw < -20000
print('裁剪：行', r0, '列', c0, '无效', int(bad.sum()), '真实高差', raw[~bad].min(), raw[~bad].max())
if bad.any():
    idx = ndi.distance_transform_edt(bad, return_distances=False, return_indices=True)
    raw = raw[tuple(idx)]
# DTM 有轻微的条纹 / 噪声（2 米格上的立体匹配误差），放大两倍后更明显：很轻地平滑一次
raw = ndi.gaussian_filter(raw, 0.8)

# 像素 (i=列, j=行) ↔ UE 米：X = (i - N//2)*STEP，Y = (j - N//2)*STEP（行向南增加 = UE +Y）
jj, ii = np.mgrid[0:N, 0:N]
X = (ii - N // 2) * STEP
Y = (jj - N // 2) * STEP

# 基准：基地范围的中位高度
core = (np.abs(X) < 260) & (np.abs(Y) < 190)
ref = float(np.median(raw[core]))
h0 = (raw - ref) * KZ
print(f'基准高度 {ref:.1f} 米（月球基准面）；放大 {KZ} 倍后整块范围 {h0.min():.1f} ~ {h0.max():.1f} 米')


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


# 2. 基地范围去低频
low = ndi.gaussian_filter(h0, 30.0)     # 约 60 米 σ
zone = 1.0 - smoothstep(0.85, 1.65, np.hypot(X / 330, (Y + 20) / 330))     # 圆的，不要方形渐变（方形的角在山坡上切出一条直线）
flat = (h0 - low) * 0.7
h = zone * flat + (1 - zone) * h0
print('基地去低频后：核心区起伏', float(h[core].min()), float(h[core].max()), '标准差', float(h[core].std()))

h_flat = h.copy()     # 去低频后、建筑压平前（大尺度明暗图用它：既不会在压平过的地方留下假坑的暗斑，也不会把建筑地基的方块边算成坡）

# 3. 建筑占地压平
items = L['items']
pad_mask_total = np.zeros_like(h)
for it in items:
    if not it['pad']:
        continue
    r = math.radians(it['yaw'])
    # 占地矩形的局部坐标
    dx, dy = X - it['cx'], Y - it['cy']
    lx = dx * math.cos(r) + dy * math.sin(r)
    ly = -dx * math.sin(r) + dy * math.cos(r)
    hw, hd = it['w'] / 2 + 4, it['d'] / 2 + 4
    outside = np.maximum(np.maximum(np.abs(lx) - hw, np.abs(ly) - hd), 0.0)
    inside = outside <= 0
    z = float(np.median(h[inside]))
    fall = 22.0 + 0.15 * max(hw, hd)
    a = 1.0 - smoothstep(0.0, fall, np.hypot(np.maximum(np.abs(lx) - hw, 0), np.maximum(np.abs(ly) - hd, 0)))
    # 里面完全平，只留一点点起伏（建筑底座盖住）
    h = h * (1 - a) + z * a
    it['pad_z'] = z
    pad_mask_total = np.maximum(pad_mask_total, a)
    print(f"  压平 {it['group']:8s} Z = {z:6.2f} 米")

nz = 1 / np.sqrt(1 + sum(g * g for g in np.gradient(h, STEP)))
slope = np.degrees(np.arccos(nz))
arena = L['arena']
inarena = (np.abs(X - arena['cx']) < arena['hx']) & (np.abs(Y - arena['cy']) < arena['hy'])
print('活动范围内 高度', float(h[inarena].min()), float(h[inarena].max()), '坡度分位 50/90/99/99.9', np.percentile(slope[inarena], [50, 90, 99, 99.9]).round(1), '超 44° 比例', float((slope[inarena] > 44).mean()))


def z_at(x, y):
    fi = np.clip(x / STEP + N // 2, 0, N - 1.001)
    fj = np.clip(y / STEP + N // 2, 0, N - 1.001)
    i, j = int(fi), int(fj)
    a, b = fi - i, fj - j
    return float(h[j, i] * (1 - a) * (1 - b) + h[j, i + 1] * a * (1 - b) + h[j + 1, i] * (1 - a) * b + h[j + 1, i + 1] * a * b)


for it in items:
    it['z'] = it.get('pad_z', None)
    if it['z'] is None:
        # 零件：按占地四角和中心的最低点放，不悬空；略微沉一点
        r = math.radians(it['yaw'])
        c, s = math.cos(r), math.sin(r)
        pts = [(it['cx'], it['cy'])]
        for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            lx, ly = sx * it['w'] / 2, sy * it['d'] / 2
            pts.append((it['cx'] + lx * c - ly * s, it['cy'] + lx * s + ly * c))
        it['z'] = min(z_at(*p) for p in pts) - 0.15
for t in L['team']:
    t['z'] = z_at(t['x'], t['y']) + 1.5
dm_out = []
for d in L['dm']:
    x, y, yaw = d[0], d[1], d[2]
    base = d[3] if len(d) > 3 else None
    dm_out.append({'x': x, 'y': y, 'yaw': yaw, 'z': (base if base is not None else z_at(x, y)) + 1.5})
L['dm'] = dm_out
# 掉出判定：场地中心 Z 以下 30 米算掉出（AMechGameMode 按 MechArena 点的 Z）：放在场内最低点以上 10 米
L['arena']['z'] = float(h[inarena].min()) + 10.0
L['arena']['terrain_max'] = float(h[inarena].max())

# ---------------------------------------------------------------- 石头（撒在地形上）：大石头有碰撞当掩体，小石头只看
rng = random.Random(20261102)
foot = []
for it in items:
    r = max(it['w'], it['d']) / 2 + 3
    if it['pad'] or r > 6:
        foot.append((it['cx'], it['cy'], r))


def blocked(x, y, extra=0.0):
    for fx, fy, r in foot:
        if (x - fx) ** 2 + (y - fy) ** 2 < (r + extra) ** 2:
            return True
    for k in L['keep']:
        if (x - k[0]) ** 2 + (y - k[1]) ** 2 < (k[2] * 0.6) ** 2:
            return True
    for t in L['team']:
        if (x - t['x']) ** 2 + (y - t['y']) ** 2 < 28 ** 2:
            return True
    for d in dm_out:
        if (x - d['x']) ** 2 + (y - d['y']) ** 2 < 10 ** 2:
            return True
    return False


rocks = []
KINDS = ['boulder_01', 'namaqualand_boulder_02', 'namaqualand_boulder_03']


def rock(x, y, size, collide):
    i, j = int(x / STEP + N // 2), int(y / STEP + N // 2)
    if not (1 <= i < N - 1 and 1 <= j < N - 1) or slope[j, i] > 32 or blocked(x, y, size * 0.5 + 1.0):
        return False
    rocks.append({'k': rng.choice(KINDS), 'x': x, 'y': y, 'z': z_at(x, y), 'yaw': rng.uniform(0, 360), 'pitch': rng.uniform(-16, 16), 'roll': rng.uniform(-16, 16), 's': size, 'c': collide})
    return True


def cluster(cx, cy, spread, big, mids, smalls, collide_big=True):
    """一堆：一块大的 + 几块中的 + 一片碎的（真实月面上的石头是成片的，不是均匀的）"""
    if big > 0:
        rock(cx, cy, big, collide_big)
    for _ in range(mids):
        a, r_ = rng.uniform(0, math.tau), abs(rng.gauss(0, spread * 0.5)) + big * 0.4
        rock(cx + math.cos(a) * r_, cy + math.sin(a) * r_, rng.uniform(1.2, max(1.5, big * 0.5)), collide_big and rng.random() < 0.35)
    for _ in range(smalls):
        a, r_ = rng.uniform(0, math.tau), abs(rng.gauss(0, spread))
        rock(cx + math.cos(a) * r_, cy + math.sin(a) * r_, rng.uniform(0.25, 1.0), False)


# 环形坑：从真实地形里找（比周围低的碗），坑边石头多
s_ = ndi.gaussian_filter(h0, 1.0)
d_ = ndi.uniform_filter(s_, size=61) - s_
lab_, nn_ = ndi.label(d_ > 2.5)
craters = []
for k_, sl in enumerate(ndi.find_objects(lab_)):
    hh, ww = sl[0].stop - sl[0].start, sl[1].stop - sl[1].start
    if hh < 6 or ww < 6:
        continue
    mask_ = lab_[sl] == k_ + 1
    yy, xx = np.mgrid[sl]
    craters.append((float(((xx[mask_].mean()) - N // 2) * STEP), float((yy[mask_].mean() - N // 2) * STEP), float(max(hh, ww)), float(d_[sl][mask_].max())))
craters.sort(key=lambda c: -c[2])
print('环形坑', len(craters), '最大几个（X, Y, 直径, 深）', [tuple(round(v) for v in c) for c in craters[:6]])
for cx, cy, dia, depth in craters:
    rim = dia * 0.5
    n_ = int(min(10, 2 + dia / 14))
    for _ in range(n_):
        a, r_ = rng.uniform(0, math.tau), rim * rng.uniform(0.85, 1.7)
        cluster(cx + math.cos(a) * r_, cy + math.sin(a) * r_, max(3.0, dia * 0.07), rng.uniform(1.5, 2.5 + dia * 0.04), rng.randint(1, 3), rng.randint(3, 9), collide_big=dia > 24)
n_crater_rocks = len(rocks)
# 基地里的矮掩体：一小堆一小堆（有碰撞的大块）
for _ in range(16):
    x, y = rng.uniform(-250, 250), rng.uniform(-185, 185)
    cluster(x, y, 5.0, rng.uniform(3.0, 5.5), rng.randint(1, 3), rng.randint(3, 8))
# 基地附近的中等石头和碎石（只看，不挡路）：飞低一点也有东西从脚下掠过
for _ in range(70):
    x, y = rng.uniform(-300, 300), rng.uniform(-230, 230)
    cluster(x, y, 4.0, rng.uniform(1.0, 2.2), rng.randint(1, 3), rng.randint(4, 12), collide_big=False)
for _ in range(1100):
    x, y = rng.uniform(-300, 300), rng.uniform(-230, 230)
    rock(x, y, rng.uniform(0.2, 0.7), False)
# 活动范围里的：大块当掩体 + 碎石
for _ in range(46):
    x, y = arena['cx'] + rng.uniform(-arena['hx'], arena['hx']), arena['cy'] + rng.uniform(-arena['hy'], arena['hy'])
    cluster(x, y, rng.uniform(5, 14), rng.uniform(3.5, 8.5), rng.randint(2, 5), rng.randint(6, 18))
for _ in range(700):
    x, y = arena['cx'] + rng.uniform(-arena['hx'], arena['hx']), arena['cy'] + rng.uniform(-arena['hy'], arena['hy'])
    rock(x, y, rng.uniform(0.25, 0.9), False)
# 场外：只看
for _ in range(120):
    x, y = rng.uniform(-990, 990), rng.uniform(-990, 990)
    if abs(x - arena['cx']) < arena['hx'] and abs(y - arena['cy']) < arena['hy']:
        continue
    cluster(x, y, rng.uniform(8, 20), rng.uniform(4, 13), rng.randint(2, 5), rng.randint(4, 12), collide_big=False)
L['rocks'] = rocks
print('石头', len(rocks), '有碰撞', sum(1 for r in rocks if r['c']))

# ---------------------------------------------------------------- 大尺度明暗图（从真实地形算，不是画的）：
# Apollo 17 照片里山坡亮、谷底暗（坡上表土一直在滑、没怎么被太阳风晒黑），新坑边缘也亮、坑底暗。R = 亮度（0.5 = 不变），G = 坡度（0~1 = 0~35°），B = 小尺度凹凸
def macro_map(z, cell, out, size):
    gy_, gx_ = np.gradient(z, cell)
    sl_ = np.degrees(np.arctan(np.hypot(gx_, gy_)))
    c1 = z - ndi.gaussian_filter(z, 6.0 / cell)       # 6 米尺度
    c2 = z - ndi.gaussian_filter(z, 40.0 / cell)      # 40 米尺度
    c3 = z - ndi.gaussian_filter(z, 300.0 / cell)     # 300 米尺度
    t_ = np.clip(sl_ / 35.0, 0, 1)
    b = 0.5 + 0.07 * smoothstep(6.0, 26.0, sl_) + np.clip(c2 * 0.012, -0.10, 0.10) + np.clip(c3 * 0.0007, -0.06, 0.06)
    cav = 0.5 + np.clip(c1 * 0.05, -0.5, 0.5)
    rgb_ = np.stack((np.clip(b, 0, 1), t_, cav), -1)
    im = Image.fromarray((rgb_ * 255).astype(np.uint8), 'RGB').resize(size, Image.LANCZOS)
    im.save(out)
    print('明暗图', out, size, '亮度分位', np.percentile(b, [1, 50, 99]).round(2))


macro_map(h_flat, STEP, f'{W}/soil/T_Moon_NearMacro.png', (1024, 1024))

# ---------------------------------------------------------------- 写 Landscape 高度图
zmin, zmax = float(h.min()), float(h.max())
zmid = (zmin + zmax) / 2
ZS = math.ceil((zmax - zmin + 4) * 100 / (65535 / 128))     # Landscape Z 缩放（厘米 / 高度单位 × 128）
h16 = np.round((h - zmid) * 100 / ZS * 128 + 32768).astype(np.int64)
assert h16.min() >= 0 and h16.max() <= 65535, (h16.min(), h16.max())
Image.fromarray(h16.astype(np.uint16)).save(f'{W}/near_height.png')
back = (h16 - 32768) / 128 * ZS / 100 + zmid
print(f'Landscape {N}×{N}，XY 缩放 {STEP*100:.0f}，Z 缩放 {ZS}，中线 Z {zmid:.2f} 米，精度 {ZS/128:.2f} 厘米，还原误差 {float(np.abs(back-h).max())*100:.2f} 厘米')
np.save(f'{W}/near.npy', h.astype(np.float32))
L['landscape'] = {'n': N, 'xy': STEP * 100, 'z': ZS, 'zmid': zmid, 'kz': KZ, 'ref': ref, 'cx_km': CX_KM, 'cy_km': CY_KM, 'r0': r0, 'c0': c0}

# ---------------------------------------------------------------- 远景：整张高程图 16 米一格（同样放大），中间 2016 米挖掉（Landscape 盖着），内部小洞用最近邻填
a = np.load(f'{W}/dtm_16m.npy').astype(np.float64)
abad = (a < -20000) | ~np.isfinite(a)
# 内部小洞（立体匹配失败、两条带中间的缝）填上；外面大片没数据的就是没有
lab, nn = ndi.label(abad)
sizes = ndi.sum_labels(abad, lab, np.arange(1, nn + 1))
small = np.isin(lab, np.arange(1, nn + 1)[sizes < 8000])     # 8000 格 × 16×16 米 = 2 平方公里以下算小洞（x≈11.7 公里那条竖缝）
idx = ndi.distance_transform_edt(abad, return_distances=False, return_indices=True)
filled = a[tuple(idx)]
valid = ~abad | small
far = (filled - ref) * KZ
far = ndi.gaussian_filter(far, 0.6)
# 远处的放大倍数逐渐减小（近处 2 倍，15 公里外 1 倍，28 公里外 0.6 倍）：不然 28 公里外的北丘（真实高差 2.7 公里 ×2）在地平线上像一座尖金字塔，坡也陡得不像月球
jf_, if_ = np.mgrid[0:far.shape[0], 0:far.shape[1]]
dkm = np.hypot(if_ * 16.0 - CX_KM * 1000, jf_ * 16.0 - CY_KM * 1000) / 1000.0
far = far * (1.0 - 0.5 * smoothstep(1.6, 15.0, dkm) - 0.2 * smoothstep(15.0, 28.0, dkm))
macro_map(far, 16.0, f'{W}/soil/T_Moon_FarMacro.png', (1024, 2048))
np.savez_compressed(f'{W}/far.npz', h=far.astype(np.float32), valid=valid, cell=16.0, x0=-(CX_KM * 1000), y0=-(CY_KM * 1000))
print('远景', far.shape, '有效格', int(valid.sum()), '填的小洞格', int((abad & small).sum()), '最高', float(far[valid].max()), '最低', float(far[valid].min()))

# ---------------------------------------------------------------- 高程图以外：NASA 这张图是不规则的一块，大山被数据边界切成竖直的断面（从基地看像地平线上立着一片鱼鳍）。
# 没有数据的地方补一张粗网格（96 米一格）：边界上和真实高度接上，往外逐渐模糊、高度慢慢落回谷底。只是填空，不是地形设计；从基地看过去至少 5 公里外
FC = 96.0
fx = np.arange(-46000.0, 52000.0, FC)
fy = np.arange(-62000.0, 44000.0, FC)
fxx, fyy = np.meshgrid(fx, fy)
ci = np.clip(((fxx + CX_KM * 1000) / 16.0).round().astype(int), 0, far.shape[1] - 1)
cj = np.clip(((fyy + CY_KM * 1000) / 16.0).round().astype(int), 0, far.shape[0] - 1)
inside_ = (fxx >= -CX_KM * 1000) & (fxx < -CX_KM * 1000 + far.shape[1] * 16) & (fyy >= -CY_KM * 1000) & (fyy < -CY_KM * 1000 + far.shape[0] * 16)
fv = valid[cj, ci] & inside_
fz = far[cj, ci].copy()
dist, ind = ndi.distance_transform_edt(~fv, sampling=FC, return_indices=True)
near_ = fz[tuple(ind)]
# 镜像：格子关于最近的边界点翻过去取真实高度，山脊的形状能接着走（不用最近点拉直线，那样地平线上是一条笔直的边）
mj_ = np.clip(2 * ind[0] - np.arange(fz.shape[0])[:, None], 0, fz.shape[0] - 1)
mi_ = np.clip(2 * ind[1] - np.arange(fz.shape[1])[None, :], 0, fz.shape[1] - 1)
mv_ = fv[mj_, mi_]
base_ = float(np.median(far[valid]))
# 边界不是直线地落回谷底：距离上叠一层低频起伏（只影响落下来的快慢），等高线就不是一圈直边
rng_f = np.random.default_rng(20261102)
warp = ndi.gaussian_filter(rng_f.standard_normal(fz.shape), 22.0)
warp = warp / warp.std()
d_w = np.maximum(dist + warp * 1400.0 * smoothstep(0.0, 1500.0, dist), 0.0)
fade = 1.0 - smoothstep(0.0, 9000.0, d_w)
val_ = np.where(mv_, fz[mj_, mi_], near_)
# 越往外越模糊，镜像线上的折痕（金字塔的棱）就没了
v1 = ndi.gaussian_filter(val_, 2.0)
v2 = ndi.gaussian_filter(val_, 8.0)
v3 = ndi.gaussian_filter(val_, 24.0)
ta, tb, tc = smoothstep(0.0, 500.0, dist), smoothstep(400.0, 2500.0, dist), smoothstep(2000.0, 7000.0, dist)
val_ = val_ * (1 - ta) + v1 * ta
val_ = val_ * (1 - tb) + v2 * tb
val_ = val_ * (1 - tc) + v3 * tc
fill = base_ + (val_ - base_) * fade
# 只要真实数据没盖到的格子（往里多留一格重叠），往下沉一点不和真实网格抢
cover = ndi.binary_erosion(fv, iterations=1)
np.savez_compressed(f'{W}/fill.npz', h=(fill - 3.0).astype(np.float32), mask=~cover, x0=fx[0], y0=fy[0], cell=FC)
print('补网格', fill.shape, '格', int((~cover).sum()), '最高', float(fill[~cover].max()))

json.dump(L, open(f'{W}/layout_final.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('写', f'{W}/layout_final.json')
print('活动范围 中心 Z', L['arena']['z'], '场内最高', L['arena']['terrain_max'])
# 四条墙线上的地形高度（判断隐形墙是不是藏在山坡里）
A_ = L['arena']
_x0, _x1, _y0, _y1 = A_['cx'] - A_['hx'], A_['cx'] + A_['hx'], A_['cy'] - A_['hy'], A_['cy'] + A_['hy']
for name, (x0, y0, x1, y1) in (('西墙', (_x0, _y0, _x0, _y1)), ('东墙', (_x1, _y0, _x1, _y1)), ('北墙', (_x0, _y0, _x1, _y0)), ('南墙', (_x0, _y1, _x1, _y1))):
    zs = [z_at(x0 + (x1 - x0) * t / 40, y0 + (y1 - y0) * t / 40) for t in range(41)]
    print(f'  {name} 地形高度 {min(zs):.0f} ~ {max(zs):.0f} 米，中位 {np.median(zs):.0f}')
