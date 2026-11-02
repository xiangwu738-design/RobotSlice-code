"""相位电子园区外观整修的改前改后对比图：同机位两张并排（左改前、右改后），每行一个机位，标题和每行说明用中文。
py -3 -I tools/k051_dress_compare.py <改前目录> <改后目录> <输出.jpg>
机位名对应 ue_techpark_shots.py 的 SHOTS（截图文件名 = 机位名.png）。"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

BEFORE, AFTER, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
ROWS = [
    ('c0_south_aerial', '全景 · 南边 55 米高空往北（大门、PEC 总部塔）'),
    ('t0_overview', '全景 · 东南高空俯看整个园区'),
    ('c1_street', '街面 · 东南角地面往西北看广场'),
    ('t2_center', '街面 · 中央交汇口往北看大门'),
    ('c2_high', '高处 · 20 号塔顶俯看广场和西基地台'),
    ('t3_deck', '高处 · 北环路桥面上'),
]
W, H = 960, 540
PAD, HEAD, LAB = 16, 120, 44
font_path = 'C:/Windows/Fonts/msyh.ttc'
big = ImageFont.truetype(font_path, 40)
mid = ImageFont.truetype(font_path, 26)
small = ImageFont.truetype(font_path, 22)
rows = [r for r in ROWS if os.path.exists(f'{BEFORE}/{r[0]}.png') and os.path.exists(f'{AFTER}/{r[0]}.png')]
sheet = Image.new('RGB', (PAD * 3 + W * 2, HEAD + len(rows) * (LAB + H + PAD)), (24, 26, 30))
d = ImageDraw.Draw(sheet)
d.text((PAD, 16), '相位电子园区 L_TechPark · 外观整修 改前 / 改后（10-31）', font=big, fill=(235, 238, 242))
d.text((PAD, 70), '改前：阴天 15 点、浅灰方砖、远景空　　改后：晴天 15 点斜照、石墨地面 + 青色嵌线、白板配石墨金属、远景楼层 + 外圈楼 + 楼顶 PHASE、空气透视',
       font=small, fill=(150, 200, 215))
y = HEAD
for name, label in rows:
    d.text((PAD, y + 8), label, font=mid, fill=(220, 224, 230))
    d.text((PAD + W - 60, y + 10), '改前', font=small, fill=(170, 170, 170))
    d.text((PAD * 2 + W * 2 - 60, y + 10), '改后', font=small, fill=(60, 200, 220))
    y += LAB
    for i, src in enumerate((BEFORE, AFTER)):
        im = Image.open(f'{src}/{name}.png').convert('RGB').resize((W, H), Image.LANCZOS)
        sheet.paste(im, (PAD + i * (W + PAD), y))
    y += H + PAD
sheet.save(OUT, quality=88)
print('对比图', OUT, sheet.size, len(rows), '行')
