"""把 ambientCG 式的散装贴图（xxx_2K_Color / Roughness / Metalness / NormalGL / AmbientOcclusion / Emission）
整理成 M_WorldPBR 要的四张：T_<名>_diff、T_<名>_nor_gl、T_<名>_arm（R=AO G=粗糙度 B=金属度）、T_<名>_emis。
py -3 pack_textures.py 散装贴图目录 输出目录
"""
import os
import re
import shutil
import sys

from PIL import Image

SRC, DST = sys.argv[1], sys.argv[2]
os.makedirs(DST, exist_ok=True)
fams = {}
for f in os.listdir(SRC):
    m = re.match(r'(.+?)_(\d+K)_([A-Za-z]+)\.(jpg|jpeg|png)$', f, re.I)
    if m:
        fams.setdefault(m.group(1), {})[m.group(3).lower()] = f
for fam, maps in sorted(fams.items()):
    if 'color' not in maps:
        print(f'[pack] 跳过 {fam}：没有底色图')
        continue
    color = Image.open(f'{SRC}/{maps["color"]}')
    size = color.size
    color.convert('RGB').save(f'{DST}/T_{fam}_diff.png')
    nor = maps.get('normalgl') or maps.get('normal')
    if nor:
        shutil.copyfile(f'{SRC}/{nor}', f'{DST}/T_{fam}_nor_gl{os.path.splitext(nor)[1].lower()}')

    def chan(key, default):
        if key in maps:
            return Image.open(f'{SRC}/{maps[key]}').convert('L').resize(size)
        return Image.new('L', size, default)

    bare_metal = fam.startswith('Metal')  # 裸金属没给金属度图的，按全金属算
    Image.merge('RGB', (chan('ambientocclusion', 255), chan('roughness', 160), chan('metalness', 255 if bare_metal else 0))).save(
        f'{DST}/T_{fam}_arm.png')
    if 'emission' in maps:
        Image.open(f'{SRC}/{maps["emission"]}').convert('RGB').save(f'{DST}/T_{fam}_emis.png')
    print(f'[pack] {fam}: {sorted(maps)}')
