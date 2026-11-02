"""看选定区域：py -3 -I tools/moon_terrain_view.py cx_km cy_km 半边公里 放大倍数 输出.png
等高线每 10 米（放大后）、坡度统计、最平的 450×350 米位置、从中心放射的 24 条射线上的最高点"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as ndi
sys.path.insert(0, 'D:/RobotSlice/tools')
from moon_dtm_survey import open_dtm
cx, cy, hw, k = [float(a) for a in sys.argv[1:5]]
out = sys.argv[5]
dtm, _ = open_dtm()
r0, r1 = int((cy - hw) * 500), int((cy + hw) * 500)
c0, c1 = int((cx - hw) * 500), int((cx + hw) * 500)
a = np.array(dtm[r0:r1, c0:c1], dtype=np.float32)
bad = (a < -20000) | ~np.isfinite(a)
print('无效', int(bad.sum()))
if bad.any():
    idx = ndi.distance_transform_edt(bad, return_distances=False, return_indices=True)
    a = a[tuple(idx)]
a = ndi.gaussian_filter(a, 0.7)
h = (a - np.median(a)) * k
gy, gx = np.gradient(h, 2.0)
sl = np.degrees(np.arctan(np.hypot(gx, gy)))
print('尺寸', a.shape, '高差', float(h.min()), float(h.max()), '坡度分位(50,90,99,99.9)', np.percentile(sl, [50, 90, 99, 99.9]).round(1), '超 44° 占比', float((sl > 44).mean()))
# 最平的 450×350 米矩形（去倾斜后的起伏）
wy, wx = 175, 225
m = ndi.uniform_filter(h, size=(wy, wx))
m2 = ndi.uniform_filter(h * h, size=(wy, wx))
var = np.sqrt(np.maximum(m2 - m * m, 0))
# 只在中间一半找
mask = np.zeros_like(var, bool)
n = var.shape[0]
mask[n // 4:3 * n // 4, n // 4:3 * n // 4] = True
var2 = np.where(mask, var, 1e9)
iy, ix = np.unravel_index(np.argmin(var2), var2.shape)
fx, fy = (c0 + ix) / 500, (r0 + iy) / 500
print('最平矩形中心 km', round(fx, 3), round(fy, 3), '标准差', float(var[iy, ix]), '当地高度', float(m[iy, ix]))
# 放射射线
z0 = float(m[iy, ix])
for an in range(0, 360, 30):
    t = np.radians(an)
    rs = np.arange(100, min(hw * 1000 - 20, 980), 4)
    xs = (ix + rs / 2 * np.cos(t)).astype(int)
    ys = (iy + rs / 2 * np.sin(t)).astype(int)
    v = np.clip(xs, 0, h.shape[1] - 1), np.clip(ys, 0, h.shape[0] - 1)
    z = h[v[1], v[0]] - z0
    print(f'方位 {an:3d}°（0=东 90=南）: 200米 {z[np.argmin(abs(rs-200))]:6.1f} 400米 {z[np.argmin(abs(rs-400))]:6.1f} 600米 {z[np.argmin(abs(rs-600))]:6.1f} 800米 {z[np.argmin(abs(rs-800))]:6.1f} 最高 {z.max():6.1f}@{rs[z.argmax()]}')
az, alt = np.radians(120), np.radians(28)
lx, ly, lz = np.cos(alt) * np.sin(az), np.cos(alt) * np.cos(az), np.sin(alt)
nz = 1 / np.sqrt(gx * gx + gy * gy + 1)
sh = np.clip((-gx * nz * lx + gy * nz * ly + nz * lz) * 1.2, 0, 1)
rgb = np.stack((sh, sh, sh), -1)
cont = np.abs(((h / 10) + 0.5) % 1 - 0.5) < 0.03 * (1 + sl / 10)
rgb[cont] = rgb[cont] * 0.5 + np.array((0.9, 0.7, 0.2)) * 0.5
cont50 = np.abs(((h / 50) + 0.5) % 1 - 0.5) < 0.012 * (1 + sl / 10)
rgb[cont50] = (1.0, 0.35, 0.1)
im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8))
d = ImageDraw.Draw(im)
f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 14)
for s in range(-int(hw * 10), int(hw * 10) + 1):
    p = int((s / 10 + hw) * 500)
    col = (0, 200, 255)
    if s % 5 == 0:
        d.line([(p, 0), (p, im.height)], fill=col, width=1)
        d.line([(0, p), (im.width, p)], fill=col, width=1)
        d.text((p + 2, 2), f'{s*100:+d}', fill=col, font=f)
        d.text((2, p + 2), f'{s*100:+d}', fill=col, font=f)
d.rectangle([ix - wx // 2, iy - wy // 2, ix + wx // 2, iy + wy // 2], outline=(0, 255, 90), width=2)
im.save(out)
print(out, im.size)
