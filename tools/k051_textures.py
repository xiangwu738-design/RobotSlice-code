"""K051 贴图整理：4K 原图 → UE 用的 2K（底色 / 法线 / 自发光）+ 1K 打包图。
- T_HTS_<材质>_D：底色（sRGB）2048
- T_HTS_<材质>_N：法线 2048，Blender 是 OpenGL 法线，翻绿通道成 DirectX
- T_HTS_<材质>_ORM：R=1（无 AO）G=粗糙度 B=金属度，1024（这些是可平铺的材质贴图，低频）
- T_HTS_<材质>_E：自发光 2048
py -3 -I tools/k051_textures.py Inbox/_k051/export
"""
import json
import os
import sys

from PIL import Image, ImageOps

SRC = sys.argv[1]
OUT = os.path.join(SRC, 'ue_tex')
os.makedirs(OUT, exist_ok=True)
data = json.load(open(os.path.join(SRC, 'export.json'), encoding='utf-8'))
for mat, info in data['materials'].items():
    t = info['textures']
    short = mat.replace('KB3D_HTS_', '')

    def load(role, mode):
        return Image.open(os.path.join(SRC, 'textures', t[role])).convert(mode) if role in t else None

    def save(im, suffix, size):
        dst = os.path.join(OUT, f'T_HTS_{short}_{suffix}.png')
        if not os.path.exists(dst):
            im.resize((size, size), Image.LANCZOS).save(dst)

    if 'D' in t:
        save(load('D', 'RGB'), 'D', 2048)
    if 'N' in t:
        r, g, b = load('N', 'RGB').split()
        save(Image.merge('RGB', (r, ImageOps.invert(g), b)), 'N', 2048)
    if 'R' in t or 'M' in t:
        size = 1024
        rough = load('R', 'L').resize((size, size), Image.LANCZOS) if 'R' in t else Image.new('L', (size, size), int(info['rough'] * 255))
        metal = load('M', 'L').resize((size, size), Image.LANCZOS) if 'M' in t else Image.new('L', (size, size), int(info['metal'] * 255))
        save(Image.merge('RGB', (Image.new('L', (size, size), 255), rough, metal)), 'ORM', size)
    if 'E' in t:
        save(load('E', 'RGB'), 'E', 2048)
    print('[tex]', short, sorted(t))
print('[tex] →', OUT, len(os.listdir(OUT)), '张')
