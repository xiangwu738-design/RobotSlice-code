"""梦核旷野图巨构墙的面板贴图：峡谷包原材质用的 Texture.jpg（格栅、小方块、刻线的灰白面板图）。
原包的做法（Scene_03 / 04 的 Material.001、Glossy）：这张图同时当底色和凹凸（Bump，物体空间，强度 0.5），墙按 5 米一格、甲板按 10 米一格平铺。
这里把它整理成 UE 用的两张 2048²：
- T_DW_Panel_C.png：底色（原图非等比拉成正方形，和 Blender 里平铺到正方形格子一样）
- T_DW_Panel_N.png：按亮度当高度、轻微模糊后求斜率得到的法线（OpenGL/DirectX 无所谓：材质里自己解码，x = 往 u 增大方向的坡、y = 往 v 增大方向（图片往下）的坡）
py -3.11 -I tools/dreamfield_panel_textures.py <Texture.jpg> <输出目录>
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

SRC, OUT = sys.argv[1], sys.argv[2]
N = 2048
K = 10.0          # 坡度放大：亮度每像素变 0.1 → 斜率 1（约 45°）；材质里还有 BumpScale 再调
os.makedirs(OUT, exist_ok=True)

im = Image.open(SRC).convert('RGB').resize((N, N), Image.LANCZOS)
im.save(f'{OUT}/T_DW_Panel_C.png')

h = np.asarray(im.convert('L').filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32) / 255.0
gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5     # 往右（u 增大）
gy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5     # 往下（v 增大）
n = np.stack([-gx * K, -gy * K, np.ones_like(h)], axis=-1)
n /= np.linalg.norm(n, axis=-1, keepdims=True)
rgb = np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)
Image.fromarray(rgb, 'RGB').save(f'{OUT}/T_DW_Panel_N.png')
tilt = np.degrees(np.arccos(n[..., 2]))
print('写出', OUT, '法线倾角 平均', round(float(tilt.mean()), 1), '95%', round(float(np.percentile(tilt, 95)), 1), '最大', round(float(tilt.max()), 1),
      '亮度 平均', round(float(h.mean()), 3), '范围', round(float(h.min()), 3), '~', round(float(h.max()), 3))
