"""原版峡谷图进游戏的截图总览：第一行左边是原包渲染图、右边是同一机位的游戏截图（都裁成 2:1），下面是其他机位。
py -3.11 -I tools/megacanyon_sheet.py 截图目录 输出.jpg
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
R = 'D:/RobotSlice/Inbox/科幻峡谷blender源文件/Files/Renders'
W = 960
ROWS = [
    [(f'{R}/1.png', '原包渲染图（Renders/1.png）', 2.0), ('1_ref_camera.png', '游戏里，同一个相机位置、朝向、视角', 2.0)],
    [('2_deck_mech.png', '甲板上（出生点），机体 2.4 米', 16 / 9), ('3_in_canyon.png', '飞进沟里往下看（沟底 -599 米）', 16 / 9)],
    [('4_lit_wall.png', '沟里 30 米深处，背光那一侧', 16 / 9), ('5_planet.png', '甲板上看行星和卡通云', 16 / 9)],
    [('6_above.png', '高处斜俯视：两面墙、甲板一直铺到地平线', 16 / 9), (f'{R}/2.png', '原包渲染图（Renders/2.png）', 16 / 9)],
]
CAP = 36
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 20)
heights = [int(W / max(r[0][2], r[1][2])) + CAP for r in ROWS]
sheet = Image.new('RGB', (2 * W, sum(heights)), (24, 24, 28))
d = ImageDraw.Draw(sheet)
y = 0
for row, hh in zip(ROWS, heights):
    for c, (p, cap, ar) in enumerate(row):
        p = p if p.startswith('D:') else os.path.join(SRC, p)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            w_, h_ = W, int(W / ar)
            r = max(w_ / im.width, h_ / im.height)
            im = im.resize((int(im.width * r + 0.5), int(im.height * r + 0.5)), Image.LANCZOS)
            x0, y0 = (im.width - w_) // 2, (im.height - h_) // 2
            sheet.paste(im.crop((x0, y0, x0 + w_, y0 + h_)), (c * W, y))
        d.text((c * W + 12, y + hh - CAP + 6), cap, fill=(230, 230, 236), font=font)
    y += hh
sheet.save(OUT, quality=90)
print('写出', OUT, sheet.size)
