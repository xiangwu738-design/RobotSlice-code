"""街景贴片的贴图：把截图拉成 2 的幂。py -3 tools/oldstreet_backdrop_tex.py 截图目录（里面有 bd_fwd.png / bd_back.png）"""
import os
import sys

from PIL import Image

src = sys.argv[1]
out = 'D:/RobotSlice/Inbox/_oldstreet/export/backdrop'
os.makedirs(out, exist_ok=True)
for a, b in (('bd_fwd', 'T_OS_Backdrop_Fwd'), ('bd_back', 'T_OS_Backdrop_Back')):
    im = Image.open(f'{src}/{a}.png').convert('RGB').resize((2048, 1024), Image.LANCZOS)
    im.save(f'{out}/{b}.png')
    print(b, im.size)
