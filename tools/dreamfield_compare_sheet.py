"""梦核旷野图墙面明暗细节的三栏对比：原图（峡谷包渲染图，可裁一块）| 改前 | 改后。
py -3.11 -I tools/dreamfield_compare_sheet.py 改前目录 改后目录 输出.jpg [改前栏标题] [改后栏标题]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

BEFORE, AFTER, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
R = 'D:/RobotSlice/Inbox/科幻峡谷blender源文件/Files/Renders'
# (原图, 原图裁剪框（像素，None = 按比例居中裁）, 截图文件名, 这一行的说明)
ROWS = [
    (f'{R}/1.png', None, '9_canyon_ref.png', '相近机位：甲板顶上顺着墙看（原图相机在沟里，我们这面墙在左边）'),
    (f'{R}/3.png', (0, 300, 1000, 862), '10_wall_mid.png', '墙面中段：原图左墙近处 vs 旷野上 150 米外、30 米高看'),
    (f'{R}/2.png', (120, 330, 1000, 825), '3_ground_up.png', '墙脚仰望：原图左墙 vs 草地上仰望'),
]
W, H, CAP, HEAD = 800, 450, 38, 44
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 21)
head = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', 26)
sheet = Image.new('RGB', (3 * W, HEAD + len(ROWS) * (H + CAP)), (24, 24, 28))
d = ImageDraw.Draw(sheet)
T1 = sys.argv[4] if len(sys.argv) > 4 else '改前'
T2 = sys.argv[5] if len(sys.argv) > 5 else '改后（面板图 + 擦墙侧光 + 雾推远）'
for i, t in enumerate(('原图（峡谷包渲染）', T1, T2)):
    d.text((i * W + 14, 8), t, fill=(240, 240, 245), font=head)


def fit(path, box=None):
    im = Image.open(path).convert('RGB')
    if box:
        im = im.crop(box)
    r = max(W / im.width, H / im.height)
    im = im.resize((int(im.width * r + 0.5), int(im.height * r + 0.5)), Image.LANCZOS)
    x0, y0 = (im.width - W) // 2, (im.height - H) // 2
    return im.crop((x0, y0, x0 + W, y0 + H))


for r, (orig, box, shot, cap) in enumerate(ROWS):
    y = HEAD + r * (H + CAP)
    for c, p in enumerate((orig, os.path.join(BEFORE, shot), os.path.join(AFTER, shot))):
        if os.path.exists(p):
            sheet.paste(fit(p, box if c == 0 else None), (c * W, y))
    d.text((14, y + H + 7), cap, fill=(225, 225, 232), font=font)
sheet.save(OUT, quality=90)
print('写出', OUT, sheet.size)
