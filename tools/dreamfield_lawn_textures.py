"""梦核旷野图的草地地面贴图：用 Megascans Kikuyu Grass（用户 10-31 下载，Inbox/梦核旷野/kikuyu_grass_wiljearqx_raw.zip）的草叶图集，
从上往下看「撒」出一张能无缝平铺的密草坪（Kikuyu 的贴图本身是草叶贴片图集、不是地面贴图，不能直接铺）。
用户 11-01：「旷野精细度太低」。做法：
- 从图集里按透明度随机裁草叶 / 草枝的小块，随机转角度、缩放，分 4 层叠到 2048² 的画布上（越下层越暗，像草丛里的阴影），
  草叶要短：材质里这张图铺 3.8 米，第一次裁的草叶 15–70 厘米长、平躺着像一地干草 → 改成 4–25 厘米的短草叶、张数多了五倍，
  贴的时候坐标取模，图的四边能接上；
- 底下是暗橄榄色的草根层；
- 每层记一个高度（下层低、上层高），最后按高度求法线。
输出（2048²）：T_DF_Lawn_C.png（底色）、T_DF_Lawn_N.png（法线，x = 往右的坡、y = 往下的坡，材质里自己解码）
py -3.11 -I tools/dreamfield_lawn_textures.py <解出来的 Kikuyu 贴图目录> <输出目录>
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

SRC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
N = 2048
rng = np.random.default_rng(20261101)
PFX = f'{SRC}/Kikuyu_Grass_wiljearqx_Raw_4K_'
ATLAS = 2048
col = np.asarray(Image.open(PFX + 'BaseColor.jpg').convert('RGB').resize((ATLAS, ATLAS), Image.LANCZOS), dtype=np.float32) / 255.0
opa = np.asarray(Image.open(PFX + 'Opacity.jpg').convert('L').resize((ATLAS, ATLAS), Image.LANCZOS), dtype=np.float32) / 255.0

canvas = np.zeros((N, N, 3), np.float32)
canvas[:] = (0.20, 0.24, 0.09)                     # 草根层：暗橄榄
# 草根层加一点大块明暗（不然一片死平）
base_noise = np.asarray(Image.fromarray((rng.random((64, 64)) * 255).astype(np.uint8)).resize((N, N), Image.BICUBIC), np.float32) / 255.0
canvas *= (0.75 + 0.5 * base_noise)[..., None]
height = np.zeros((N, N), np.float32)


def crop():
    """从图集里裁一块有东西的草叶（左半边是长草叶、右半边是带小叶的草枝）"""
    for _ in range(50):
        w, h = int(rng.integers(36, 90)), int(rng.integers(70, 200))
        x, y = int(rng.integers(0, ATLAS - w)), int(rng.integers(0, ATLAS - h))
        a = opa[y:y + h, x:x + w]
        if a.mean() > 0.07:
            # 裁出来的草叶两头是刀切的直边，铺一地像小木棍 → 沿长度方向两头淡出；透明度压一下阈值，免得裁框里半透明的底色叠成一个个方块
            t = np.linspace(0.0, 1.0, h, dtype=np.float32)[:, None]
            taper = np.clip(np.sin(np.pi * t), 0.0, 1.0) ** 0.8
            a2 = np.clip((a - 0.12) / 0.33, 0.0, 1.0)
            return col[y:y + h, x:x + w], a2 * taper
    return col[:200, :80], opa[:200, :80]


LAYERS = [(5600, 0.55, 0.25), (5200, 0.7, 0.5), (4400, 0.85, 0.75), (3600, 1.0, 1.0)]   # 张数、亮度、高度
for count, bright, hval in LAYERS:
    for _ in range(count):
        c, a = crop()
        s = float(rng.uniform(0.3, 0.65))
        ang = float(rng.uniform(0, 360))
        rgba = np.dstack([c, a])
        im = Image.fromarray((rgba * 255).astype(np.uint8), 'RGBA')
        im = im.resize((max(4, int(im.width * s)), max(4, int(im.height * s))), Image.BILINEAR).rotate(ang, Image.BILINEAR, expand=True)
        arr = np.asarray(im, np.float32) / 255.0
        h, w = arr.shape[:2]
        x0, y0 = int(rng.integers(0, N)), int(rng.integers(0, N))
        ys = (np.arange(h) + y0) % N
        xs = (np.arange(w) + x0) % N
        al = arr[..., 3:4]
        tint = np.array([rng.uniform(0.9, 1.1), rng.uniform(0.95, 1.08), rng.uniform(0.85, 1.05)], np.float32) * rng.uniform(0.78, 1.0)
        sub = canvas[np.ix_(ys, xs)]
        canvas[np.ix_(ys, xs)] = sub * (1 - al) + arr[..., :3] * bright * tint * al
        hs = height[np.ix_(ys, xs)]
        height[np.ix_(ys, xs)] = np.maximum(hs, al[..., 0] * hval)

# 颜色：整体拉到偏亮的黄绿、平均亮度约 0.5（材质里再乘大尺度的草色，这张只当细节）
lum = canvas @ np.array([0.2126, 0.7152, 0.0722], np.float32)
canvas *= 0.5 / max(float(lum.mean()), 1e-3)
canvas = np.clip(canvas, 0, 1)
Image.fromarray((canvas * 255 + 0.5).astype(np.uint8), 'RGB').save(f'{OUT}/T_DF_Lawn_C.png')

hb = np.asarray(Image.fromarray((height * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 255.0
gx = (np.roll(hb, -1, 1) - np.roll(hb, 1, 1)) * 0.5
gy = (np.roll(hb, -1, 0) - np.roll(hb, 1, 0)) * 0.5
K = 6.0
n = np.dstack([-gx * K, -gy * K, np.ones_like(hb)])
n /= np.linalg.norm(n, axis=-1, keepdims=True)
Image.fromarray(np.clip((n * 0.5 + 0.5) * 255 + 0.5, 0, 255).astype(np.uint8), 'RGB').save(f'{OUT}/T_DF_Lawn_N.png')
print('写出', OUT, '覆盖率（有草叶的像素）', round(float((height > 0.2).mean()), 3), '平均亮度', round(float((canvas @ np.array([0.2126, 0.7152, 0.0722])).mean()), 3))
