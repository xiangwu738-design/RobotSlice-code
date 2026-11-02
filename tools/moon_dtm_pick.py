"""月球基地：在高程图上按分数挑选 2 公里见方的候选区。
分数看三件事：中央 450×350 米够平（放得下基地）、四周 400~1000 米各方向都有比中央高的东西（天然边界）、整块没有无效值。
py -3 -I tools/moon_dtm_pick.py  → 打印前 20 名，写 work/pick_overview.png"""
import sys
import numpy as np
from scipy import ndimage as ndi
from PIL import Image, ImageDraw, ImageFont
W = 'D:/RobotSlice/Inbox/_moon_nasa/work'
a = np.load(f'{W}/dtm_16m.npy').astype(np.float64)
bad = (a < -20000) | ~np.isfinite(a)
h, w = a.shape
PX = 16.0
# 无效值填最近邻（只用来算分，带 bad 的候选区扣掉）
idx = ndi.distance_transform_edt(bad, return_distances=False, return_indices=True)
f = a[tuple(idx)]
Rc = int(225 / PX)
step = int(100 / PX)
rays = 24
res = []
badint = ndi.uniform_filter(bad.astype(float), size=int(2000 / PX))
cx_list = range(int(1000 / PX), w - int(1000 / PX), step)
cy_list = range(int(1000 / PX), h - int(1000 / PX), step)
angs = np.linspace(0, 2 * np.pi, rays, endpoint=False)
rr = np.arange(350, 1000, 32) / PX
for cy in cy_list:
    for cx in cx_list:
        if badint[cy, cx] > 0.001:
            continue
        # 中央块：拟合平面，去掉倾斜后的高差 + 坡度
        c = f[cy - int(175 / PX):cy + int(175 / PX) + 1, cx - Rc:cx + Rc + 1]
        yy, xx = np.mgrid[:c.shape[0], :c.shape[1]]
        A = np.c_[xx.ravel(), yy.ravel(), np.ones(c.size)]
        coef, *_ = np.linalg.lstsq(A, c.ravel(), rcond=None)
        res_plane = c - (A @ coef).reshape(c.shape)
        rough = res_plane.std()
        tilt = np.degrees(np.arctan(np.hypot(coef[0], coef[1]) / PX))
        # 去倾斜后的四周地平：沿每条射线，取最高点相对中央平面的高差
        z0 = coef[2] + coef[0] * (c.shape[1] / 2) + coef[1] * (c.shape[0] / 2)
        rims = []
        for an in angs:
            xs = (cx + rr * np.cos(an)).astype(int)
            ys = (cy + rr * np.sin(an)).astype(int)
            zs = f[ys, xs]
            rims.append((zs - z0).max())
        rims = np.array(rims)
        res.append((cx, cy, rough, tilt, np.percentile(rims, 15), np.median(rims), rims.min(), rims.max()))
res = np.array(res)
print('候选', len(res))
# 分数：四周最低的那 15% 方向的高度（越高越围得住），扣中央不平；倾斜 > 6° 的不要
score = np.clip(res[:, 4], 0, 120) * 1.0 + np.clip(res[:, 5], 0, 200) * 0.25 - res[:, 2] * 8.0 - np.clip(res[:, 3] - 2, 0, None) * 6
ok = res[:, 3] < 8
order = np.argsort(-score * ok)
picked = []
for i in order:
    if not ok[i]:
        continue
    cx, cy = res[i, 0], res[i, 1]
    if any(np.hypot(cx - p[0], cy - p[1]) < 1200 / PX for p in picked):
        continue
    picked.append((cx, cy, i))
    if len(picked) >= 16:
        break
print('排名 中心(公里 x,y) 中央粗糙度 倾斜 四周15%分位 中位 最低 最高 分数')
for k, (cx, cy, i) in enumerate(picked):
    r = res[i]
    print(k, f'x={cx*PX/1000:.1f} y={cy*PX/1000:.1f} 粗糙 {r[2]:.1f} 米 倾斜 {r[3]:.1f}° 四周 p15 {r[4]:.0f} 中位 {r[5]:.0f} 最低 {r[6]:.0f} 最高 {r[7]:.0f} 分 {score[i]:.0f}')
