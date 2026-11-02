"""月球基地：Poly Haven 月壤贴图 4K → 2K，并去掉贴图里的大尺度明暗（高通）：不然一张贴图重复铺开时，上百米外能看到一格一格的网格。
输出底色 / OpenGL 法线 / 粗糙度，平均亮度统一，材质里按比例相乘。地球图也压一下。py -3 -I tools/moon_soil_textures.py"""
import os

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

Image.MAX_IMAGE_PIXELS = None
SRC = 'D:/RobotSlice/Inbox/_polyhaven/textures'
OUT = 'D:/RobotSlice/Inbox/_moon_nasa/work/soil'
os.makedirs(OUT, exist_ok=True)
SIGMA = 110       # 2048 像素里 110 像素以上的起伏都当大尺度明暗去掉（周期边界，贴图仍然四方连续）
MEAN = 0.45       # 处理后的平均亮度（sRGB 0~1），材质按这个算回 1
for fam in ('moon_01', 'moon_dusted_01', 'moon_dusted_03', 'moon_meteor_01'):
    d = Image.open(f'{SRC}/{fam}/{fam}_diff_4k.png').convert('RGB').resize((2048, 2048), Image.LANCZOS)
    a = np.asarray(d).astype(np.float32) / 255.0
    lum = a.mean(-1)
    low = ndi.gaussian_filter(lum, SIGMA, mode='wrap')
    out = a / low[..., None] * MEAN
    print(fam, '底色 高通前亮度标准差', round(float(lum.std()), 4), '高通后', round(float(out.mean(-1).std()), 4))
    Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(f'{OUT}/T_Moon_{fam}_D.png')
    n = Image.open(f'{SRC}/{fam}/{fam}_nor_gl_4k.png').convert('RGB').resize((2048, 2048), Image.LANCZOS)
    v = np.asarray(n).astype(np.float32) / 127.5 - 1.0
    for c in (0, 1):
        v[..., c] -= ndi.gaussian_filter(v[..., c], SIGMA, mode='wrap')
    v[..., 2] = np.sqrt(np.clip(1.0 - v[..., 0] ** 2 - v[..., 1] ** 2, 0.0, 1.0))
    Image.fromarray(np.clip((v + 1.0) * 127.5 + 0.5, 0, 255).astype(np.uint8)).save(f'{OUT}/T_Moon_{fam}_N.png')
    r = Image.open(f'{SRC}/{fam}/{fam}_rough_4k.png')
    if r.mode.startswith('I'):
        r = Image.fromarray((np.asarray(r).astype(np.float32) / 65535.0 * 255).astype(np.uint8))
    r.convert('L').resize((2048, 2048), Image.LANCZOS).save(f'{OUT}/T_Moon_{fam}_R.png')
im = Image.open('D:/RobotSlice/Inbox/_polyhaven/earth/world.topo.bathy.200412.3x5400x2700.jpg').convert('RGB')
im.resize((4096, 2048), Image.LANCZOS).save(f'{OUT}/T_Moon_Earth.png')
print('完成')
