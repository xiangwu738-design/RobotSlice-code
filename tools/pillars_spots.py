"""石柱山水（L_Pillars）的分析工具（不产出任何游戏资源）：把地形和石柱投成俯视高度图，挑能站的石柱顶 / 岛当死斗重生点的候选。
py -3 tools/pillars_spots.py [S 放大倍数，默认 6]
读 Inbox/_f25/export/bake.npz（tools/blender_pillars_export.py 写的）；写 Inbox/_f25/export/spots.json、height.npz、Saved/pillars_height.png（检查用）。
"""
import json
import os
import sys

import numba
import numpy as np
from PIL import Image
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None
ROOT = 'D:/RobotSlice'
EXP = f'{ROOT}/Inbox/_f25/export'
S = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
RES = 4096
CX, CY, HALF = 10.0, 10.0, 85.0  # 烤的范围（原作单位）：x -75..95, y -75..95，把所有石柱都包进去
DMAX, SHORE = 3.0, 4.0
meta = json.load(open(f'{EXP}/pillars.json', encoding='utf-8'))
WZ = meta['water_z']
d = np.load(f'{EXP}/bake.npz')
cmap = np.asarray(Image.open(f'{ROOT}/Inbox/_f25/textures/Colormap_0.png').convert('RGB'), dtype=np.float32) / 255.0


@numba.njit(fastmath=True)
def raster_terrain(tv, tuv, res, x0, y0, step, cmap, H, C):
    n = tv.shape[0] // 3
    ch, cw = cmap.shape[0], cmap.shape[1]
    for i in range(n):
        a, b, c = tv[3 * i], tv[3 * i + 1], tv[3 * i + 2]
        ua, ub, uc = tuv[3 * i], tuv[3 * i + 1], tuv[3 * i + 2]
        ax, ay = (a[0] - x0) / step, (a[1] - y0) / step
        bx, by = (b[0] - x0) / step, (b[1] - y0) / step
        cx, cy = (c[0] - x0) / step, (c[1] - y0) / step
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        mnx, mxx = max(int(min(ax, bx, cx)), 0), min(int(max(ax, bx, cx)) + 1, res - 1)
        mny, mxy = max(int(min(ay, by, cy)), 0), min(int(max(ay, by, cy)) + 1, res - 1)
        for py in range(mny, mxy + 1):
            for px in range(mnx, mxx + 1):
                w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
                w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
                w2 = 1.0 - w0 - w1
                if w0 < -0.02 or w1 < -0.02 or w2 < -0.02:
                    continue
                z = w0 * a[2] + w1 * b[2] + w2 * c[2]
                if z > H[py, px]:
                    H[py, px] = z
                    u = w0 * ua[0] + w1 * ub[0] + w2 * uc[0]
                    v = w0 * ua[1] + w1 * ub[1] + w2 * uc[1]
                    tx = int((u % 1.0) * (cw - 1))
                    ty = int(((1.0 - v) % 1.0) * (ch - 1))
                    C[py, px] = cmap[ty, tx]


@numba.njit(fastmath=True)
def raster_rock(rv, rt, res, x0, y0, step, H):
    for i in range(rt.shape[0]):
        a, b, c = rv[rt[i, 0]], rv[rt[i, 1]], rv[rt[i, 2]]
        ax, ay = (a[0] - x0) / step, (a[1] - y0) / step
        bx, by = (b[0] - x0) / step, (b[1] - y0) / step
        cx, cy = (c[0] - x0) / step, (c[1] - y0) / step
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        mnx, mxx = max(int(min(ax, bx, cx)), 0), min(int(max(ax, bx, cx)) + 1, res - 1)
        mny, mxy = max(int(min(ay, by, cy)), 0), min(int(max(ay, by, cy)) + 1, res - 1)
        for py in range(mny, mxy + 1):
            for px in range(mnx, mxx + 1):
                w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
                w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
                w2 = 1.0 - w0 - w1
                if w0 < -0.02 or w1 < -0.02 or w2 < -0.02:
                    continue
                z = w0 * a[2] + w1 * b[2] + w2 * c[2]
                if z > H[py, px]:
                    H[py, px] = z


x0, y0 = CX - HALF, CY - HALF
step = 2 * HALF / RES
Ht = np.full((RES, RES), -100.0, dtype=np.float32)
Hr = np.full((RES, RES), -100.0, dtype=np.float32)
C = np.zeros((RES, RES, 3), dtype=np.float32)
raster_terrain(d['terr_v'].astype(np.float32), d['terr_uv'].astype(np.float32), RES, x0, y0, step, cmap, Ht, C)
raster_rock(d['rock_v'].astype(np.float32), d['rock_t'], RES, x0, y0, step, Hr)
print('地形覆盖', (Ht > -50).mean(), '石柱覆盖', (Hr > -50).mean())

H = np.maximum(Ht, Hr)
land = H > WZ
# ---------------------------------------------------------------- 找落脚点：高度图上取半径 R 内起伏小的连通块
R_M = 5.0   # 机体站稳要的半径（米，放大后）
rad = max(2, int(R_M / S / step))
hmax = ndimage.maximum_filter(H, size=2 * rad + 1)
hmin = ndimage.minimum_filter(H, size=2 * rad + 1)
flat = (hmax - hmin < 0.55 * R_M / S) & (H > WZ + 0.25)  # 约 30° 以内
lab, n = ndimage.label(flat)
spots = []
for i, sl in enumerate(ndimage.find_objects(lab)):
    area = (lab[sl] == i + 1).sum() * (step * S) ** 2
    if area < 25:
        continue
    mask = lab[sl] == i + 1
    dist = ndimage.distance_transform_edt(mask)
    p = np.unravel_index(dist.argmax(), dist.shape)
    py, px = p[0] + sl[0].start, p[1] + sl[1].start
    spots.append({'x': round(float(x0 + (px + 0.5) * step), 2), 'y': round(float(y0 + (py + 0.5) * step), 2), 'z': round(float(H[py, px]), 2),
                  'area_m2': round(float(area)), 'r_m': round(float(dist.max() * step * S), 1), 'rock': bool(Hr[py, px] > Ht[py, px])})
spots.sort(key=lambda s: -s['z'])
print('岛上候选', len(spots))

# 石柱顶：要求放宽（半径 3 米、约 40° 以内），只看高出水面 3 单位以上的石柱
R2 = max(2, int(3.0 / S / step))
hmax2 = ndimage.maximum_filter(H, size=2 * R2 + 1)
hmin2 = ndimage.minimum_filter(H, size=2 * R2 + 1)
flat2 = (hmax2 - hmin2 < 0.85 * 3.0 / S) & (H > WZ + 3.0) & (Hr > Ht)
lab2, n2 = ndimage.label(flat2)
tops = []
for i, sl in enumerate(ndimage.find_objects(lab2)):
    mask = lab2[sl] == i + 1
    area = mask.sum() * (step * S) ** 2
    if area < 8:
        continue
    dist = ndimage.distance_transform_edt(mask)
    p = np.unravel_index(dist.argmax(), dist.shape)
    py, px = p[0] + sl[0].start, p[1] + sl[1].start
    tops.append({'x': round(float(x0 + (px + 0.5) * step), 2), 'y': round(float(y0 + (py + 0.5) * step), 2), 'z': round(float(H[py, px]), 2),
                 'area_m2': round(float(area)), 'r_m': round(float(dist.max() * step * S), 1), 'rock': True})
tops.sort(key=lambda s: -s['z'])
print('石柱顶候选', len(tops))
for t in tops[:30]:
    print(t)

# 两队出生：主石柱群里最大的两块岛，离得够远（原作单位 x < 30）
big = sorted((s for s in spots if s['x'] < 30 and s['r_m'] >= 5), key=lambda s: -s['area_m2'])
team = None
best = 0.0
for i, a_ in enumerate(big[:10]):
    for b_ in big[i + 1:10]:
        d = ((a_['x'] - b_['x']) ** 2 + (a_['y'] - b_['y']) ** 2) ** 0.5
        score = d * min(a_['r_m'], b_['r_m']) ** 0.5
        if score > best and d * S > 200:
            best = score
            team = [a_, b_]
print('两队出生', team, '相距', ((team[0]['x'] - team[1]['x']) ** 2 + (team[0]['y'] - team[1]['y']) ** 2) ** 0.5 * S, '米')

# 死斗重生点：岛和石柱顶都要（石柱顶优先取高的），互相隔 25 米以上、铺满
dm = []
def far_enough(c, d_m=25.0):
    return all(((c['x'] - o['x']) ** 2 + (c['y'] - o['y']) ** 2) ** 0.5 * S >= d_m for o in dm)
for t in tops:
    if len([o for o in dm if o['rock']]) >= 10:
        break
    if far_enough(t, 40.0):
        dm.append(t)
for c in sorted(spots, key=lambda s: -s['r_m']):
    if len(dm) >= 26:
        break
    if far_enough(c):
        dm.append(c)
print('死斗重生点', len(dm), '其中石柱顶', len([o for o in dm if o['rock']]))
for sp in dm:
    if sp['x'] > 27.0:
        sp['z'] = round(sp['z'] - 2.0, 2)  # ue_build_pillars.py 的 EAST_SINK
json.dump({'S': S, 'spots': spots, 'tops': tops, 'team': team, 'dm': dm}, open(f'{EXP}/spots.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
# 岛上的植物：地形朝上的缓坡，一片一片的（用模糊过的随机场决定哪里长），树、灌木、蕨、草各一批
rng = np.random.default_rng(20261102)
gy, gx = np.gradient(H.astype(np.float32), step)
slope = np.hypot(gx, gy)
field = ndimage.gaussian_filter(rng.random((RES // 4, RES // 4)).astype(np.float32), 10)
field = (field - field.min()) / (field.max() - field.min())
field = ndimage.zoom(field, 4, order=1)[:RES, :RES]
plantable = (Ht >= Hr) & (H > WZ + 0.14)
keep_out = np.zeros_like(plantable)
for sp in list(team):
    py = int((sp['y'] - y0) / step)
    px = int((sp['x'] - x0) / step)
    r = int(14.0 / S / step)
    keep_out[py - r:py + r, px - r:px + r] = True
plantable &= ~keep_out
plants = []
for kind, count, smax, fmin in (('tree', 260, 0.45, 0.52), ('bush', 480, 0.6, 0.45), ('fern', 900, 0.55, 0.42), ('grass', 3600, 0.7, 0.35)):
    ok = plantable & (slope < smax) & (field > fmin)
    ys, xs = np.nonzero(ok)
    idx = rng.choice(len(ys), size=min(count, len(ys)), replace=False)
    for i in idx:
        plants.append([round(float(x0 + (xs[i] + 0.5) * step), 3), round(float(y0 + (ys[i] + 0.5) * step), 3), round(float(H[ys[i], xs[i]]), 3), kind])
    print('植物', kind, len(idx), '可种的像素', len(ys))
spots_all = json.load(open(f'{EXP}/spots.json', encoding='utf-8'))
spots_all['plants'] = plants
json.dump(spots_all, open(f'{EXP}/spots.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
vis = np.clip((H - WZ) / 14.0, 0, 1)
vis_img = np.dstack([vis, vis, vis])
vis_img[~land] = np.array([0.1, 0.25, 0.4])
vis_img[flat] = vis_img[flat] * np.array([0.4, 1.0, 0.4]) + np.array([0.0, 0.2, 0.0])
Image.fromarray((np.clip(vis_img, 0, 1) * 255).astype(np.uint8)).resize((1400, 1400)).save(f'{ROOT}/Saved/pillars_height.png')
np.savez_compressed(f'{EXP}/height.npz', H=H.astype(np.float32), x0=x0, y0=y0, step=step, land=land)
