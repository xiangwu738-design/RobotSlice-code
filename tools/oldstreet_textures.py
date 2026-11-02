"""老旧街道贴图整理：Inbox/_oldstreet/textures → Inbox/_oldstreet/export/tex。全是原包的贴图，只做三件事：拉成 2 的幂（不然压不成 BC）、法线翻绿（OpenGL→DirectX）、粗糙度 / AO 打包成 ARM。
py -3 -I tools/oldstreet_textures.py
"""
import os
from PIL import Image

SRC = 'D:/RobotSlice/Inbox/_oldstreet/textures'
OUT = 'D:/RobotSlice/Inbox/_oldstreet/export/tex'
os.makedirs(OUT, exist_ok=True)
Image.MAX_IMAGE_PIXELS = None

COLOR = {
    'T_OS_OldHouse_D': ('旧房屋.jpg', (1024, 2048)),
    'T_OS_Clock_D': ('QQ图片20250316215837.jpg', (1024, 2048)),
    'T_OS_Photo_D': ('QQ图片20250316215833.jpg', (1024, 2048)),
    'T_OS_Mart_D': ('36524.jpg', (2048, 2048)),
    'T_OS_Road_D': ('Road003_1K-JPG_Color.jpg', (1024, 1024)),
    'T_OS_Paving_D': ('PavingStones131_1K-JPG_Color.jpg', (1024, 1024)),
    'T_OS_Curb_D': ('路缘石.jpg', (512, 512)),
    'T_OS_Stall_D': ('F8~DQP[T5{QW7D[@4~E$(55.jpg', (1024, 1024)),
    'T_OS_StallSide_D': ('屏幕截图 2025-02-26 214852.png', (1024, 1024)),
    'T_OS_Box_D': ('Diffuse_Bake.jpg', (1024, 1024)),
    'T_OS_Wood_D': ('木纹.jpg', (512, 512)),
    'T_OS_Sign_D': ('标志.jpg', (512, 512)),
    'T_OS_Ped_D': ('行人信号灯.jpg', (512, 512)),
    'T_OS_Bark_D': ('bq_Bark_Aesculus-hippocastanum_Diffuse.jpg', (1024, 2048)),
}
NORMAL = {
    'T_OS_Road_N': ('Road003_1K-JPG_NormalGL.jpg', (1024, 1024)),
    'T_OS_Paving_N': ('PavingStones131_1K-JPG_NormalGL.jpg', (1024, 1024)),
    'T_OS_Box_N': ('Normal_Bake.jpg', (1024, 1024)),
    'T_OS_Bark_N': ('bq_Bark_Aesculus-hippocastanum_Normal.jpg', (1024, 2048)),
}
ARM = {   # R = AO，G = 粗糙度，B = 金属度（都没有金属）
    'T_OS_Road_ARM': (None, 'Road003_1K-JPG_Roughness.jpg', (512, 512)),
    'T_OS_Paving_ARM': ('PavingStones131_1K-JPG_AmbientOcclusion.jpg', 'PavingStones131_1K-JPG_Roughness.jpg', (512, 512)),
}

for name, (f, size) in COLOR.items():
    im = Image.open(f'{SRC}/{f}').convert('RGB')
    if im.size != size:
        im = im.resize(size, Image.LANCZOS)
    im.save(f'{OUT}/{name}.png')
    print(name, f, im.size)
for name, (f, size) in NORMAL.items():
    im = Image.open(f'{SRC}/{f}').convert('RGB')
    if im.size != size:
        im = im.resize(size, Image.LANCZOS)
    r, g, b = im.split()
    Image.merge('RGB', (r, g.point(lambda v: 255 - v), b)).save(f'{OUT}/{name}.png')
    print(name, f, im.size)
for name, (ao, rough, size) in ARM.items():
    g = Image.open(f'{SRC}/{rough}').convert('L').resize(size, Image.LANCZOS)
    r = Image.open(f'{SRC}/{ao}').convert('L').resize(size, Image.LANCZOS) if ao else Image.new('L', size, 255)
    Image.merge('RGB', (r, g, Image.new('L', size, 0))).save(f'{OUT}/{name}.png')
    print(name, size)

# 树叶：原包的叶子贴图丢了（生成的 UV 格子）。用 K059 包的树叶图集（Inbox/_k059/export_k059/raw，城区图在用）裁左下象限：一格 7 小叶的复叶 + 两格单叶。
# 底色 + 透明度合成 RGBA（2048），法线翻绿（1024）
RAW = 'D:/RobotSlice/Inbox/_k059/export_k059/raw/KB3D_CTS_LeafAtlas'
box = (0, 2048, 2048, 4096)
col = Image.open(RAW + '_basecolor.png').convert('RGB').crop(box)
opa = Image.open(RAW + '_opacity.png').convert('L').crop(box)
col.putalpha(opa)
col.save(f'{OUT}/T_OS_Leaf_D.png')
nor = Image.open(RAW + '_normal.png').convert('RGB').crop(box).resize((1024, 1024), Image.LANCZOS)
r, g, b = nor.split()
Image.merge('RGB', (r, g.point(lambda v: 255 - v), b)).save(f'{OUT}/T_OS_Leaf_N.png')
print('T_OS_Leaf_D / _N')
