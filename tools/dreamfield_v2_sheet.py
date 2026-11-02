"""梦核旷野图第二版（11-01）的改前 | 改后两栏对比：平台和电梯、封锁的大门、旷野近景和远景、墙面（去掉中层台沿）。
py -3.11 -I tools/dreamfield_v2_sheet.py 改前目录 改后目录 输出.jpg
改前、改后同名机位；第二版新加的机位（11–14）改前那一栏是旧平台上同一个相对位置。
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

BEFORE, AFTER, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
# (截图文件名, 这一行的说明)
ROWS = [
    ('11_platform_front.png', '平台正面：70×300×12 米 → 110×450×24 米，两部电梯，墙上三扇封锁的大门'),
    ('5_overview.png', '高处俯视全图：平台加大加高、两头坡道加长'),
    ('13_elevator.png', '电梯：14 米见方的轿厢，钢井架敞开朝旷野，上下口刷黄黑警示条'),
    ('12_gate.png', '平台上看中间那扇大门：门扇焊死、三道加固横梁、门缝警示条、门楣一排红灯'),
    ('4_platform_along.png', '顺着平台看：平台加高加长，坡道 120 米'),
    ('10_wall_mid.png', '墙面中段：去掉了约 114 米高的那排「中层台沿」'),
    ('14_field_near.png', '旷野近景：地面换成 Kikuyu 草叶撒出来的草坪贴图，草分高草片 / 矮草坪，加近处细草和小株植物'),
    ('7_grass_mech.png', '机体视角看草地'),
    ('1_far_rise.png', '旷野远景'),
]
W, H, CAP, HEAD = 960, 540, 40, 46
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 22)
head = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', 27)
sheet = Image.new('RGB', (2 * W, HEAD + len(ROWS) * (H + CAP)), (24, 24, 28))
d = ImageDraw.Draw(sheet)
for i, t in enumerate(('改前（第一版，史诗画质）', '改后（第二版，史诗画质）')):
    d.text((i * W + 14, 8), t, fill=(240, 240, 245), font=head)


def fit(path):
    im = Image.open(path).convert('RGB')
    r = max(W / im.width, H / im.height)
    im = im.resize((int(im.width * r + 0.5), int(im.height * r + 0.5)), Image.LANCZOS)
    x0, y0 = (im.width - W) // 2, (im.height - H) // 2
    return im.crop((x0, y0, x0 + W, y0 + H))


for r, (shot, cap) in enumerate(ROWS):
    y = HEAD + r * (H + CAP)
    for c, p in enumerate((os.path.join(BEFORE, shot), os.path.join(AFTER, shot))):
        if os.path.exists(p):
            sheet.paste(fit(p), (c * W, y))
    d.text((14, y + H + 8), cap, fill=(225, 225, 232), font=font)
sheet.save(OUT, quality=88)
print('写出', OUT, sheet.size)
