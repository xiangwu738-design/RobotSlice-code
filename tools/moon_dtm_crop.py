"""月球基地：高程图裁一块看细节。py -3 -I tools/moon_dtm_crop.py x0_km y0_km x1_km y1_km 米每像素 输出.png [高程放大倍数，仅影响阴影]
图上每 500 米一条线，标原图公里数；颜色 = 相对高度（用来判断哪里平、哪里有坑）"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, 'D:/RobotSlice/tools')
from moon_dtm_survey import open_dtm
x0, y0, x1, y1 = [float(a) for a in sys.argv[1:5]]
mpp = float(sys.argv[5])
out = sys.argv[6]
zk = float(sys.argv[7]) if len(sys.argv) > 7 else 1.0
dtm, _ = open_dtm()
s = max(1, int(round(mpp / 2)))
a = np.array(dtm[int(y0 * 500):int(y1 * 500):s, int(x0 * 500):int(x1 * 500):s], dtype=np.float32)
bad = (a < -20000) | ~np.isfinite(a)
good = a[~bad]
a[bad] = np.median(good)
print('范围', good.min(), good.max(), '分位', np.percentile(good, [1, 10, 50, 90, 99]).round(1))
gy, gx = np.gradient(a * zk, s * 2.0)
az, alt = np.radians(135), np.radians(30)
lx, ly, lz = np.cos(alt) * np.sin(az), np.cos(alt) * np.cos(az), np.sin(alt)
nz = 1 / np.sqrt(gx * gx + gy * gy + 1)
sh = np.clip((-gx * nz * lx + gy * nz * ly + nz * lz) * 1.15, 0, 1)
lo, hi = np.percentile(good, [1, 99])
t = np.clip((a - lo) / max(hi - lo, 1), 0, 1)
col = np.stack((0.35 + 0.65 * t, 0.65 - 0.25 * np.abs(t - 0.5), 1.0 - 0.7 * t), -1)
rgb = (np.clip(col * sh[..., None] * 1.1, 0, 1) * 255).astype(np.uint8)
rgb[bad] = (120, 30, 30)
im = Image.fromarray(rgb)
d = ImageDraw.Draw(im)
f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 13)
p = 1000 / (s * 2.0)
import math
for k in range(int(math.ceil(y0 * 2)), int(y1 * 2) + 1):
    y = int((k / 2 - y0) * p)
    d.line([(0, y), (im.width, y)], fill=(255, 220, 0) if k % 2 == 0 else (160, 140, 0), width=1)
    d.text((2, y + 1), f'y{k/2:g}', fill=(255, 240, 0), font=f)
for k in range(int(math.ceil(x0 * 2)), int(x1 * 2) + 1):
    x = int((k / 2 - x0) * p)
    d.line([(x, 0), (x, im.height)], fill=(0, 220, 255) if k % 2 == 0 else (0, 130, 160), width=1)
    d.text((x + 2, 16), f'x{k/2:g}', fill=(0, 240, 255), font=f)
im.save(out)
print(out, im.size)
