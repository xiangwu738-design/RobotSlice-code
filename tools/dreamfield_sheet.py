"""梦核旷野图的截图总览：左上放峡谷包原渲染图（参考），其余是 L_Dreamfield 的几个机位，每张下面写一行说明。
py -3.11 -I tools/dreamfield_sheet.py 截图目录 输出.jpg
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
REF = 'D:/RobotSlice/Inbox/科幻峡谷blender源文件/Files/Renders/2.png'
ITEMS = [
    (REF, '参考：科幻峡谷包原渲染图（两面墙夹一条沟）'),
    ('1_far_rise.png', '远看（1.2 公里）：巨构墙从草地里拔地而起，墙高 200 米'),
    ('3_ground_up.png', '墙脚草地上仰望：楼柱群 + 甲板檐口，墙后挂着大行星'),
    ('7_grass_mech.png', '草地上的机体（2.4 米）：草丛、白花、平台和坡道'),
    ('2_platform_out.png', '站在二级平台上（高 12 米）往旷野看'),
    ('4_platform_along.png', '平台上顺着墙看：平台进深 70 米，墙脚楼柱插进平台'),
    ('5_overview.png', '高处斜看：平台在墙脚，中层台沿把墙分成两层'),
    ('6_field_sky.png', '背对墙：空旷的草地、白花、远处的矮丘和粉色地平线'),
    ('8_hills.png', '离地 14 米往旷野看：两翼和远处平缓的丘陵'),
]
W, H = 800, 450
CAP = 40
cols = 3
items = [(p if os.path.isabs(p) or p.startswith('D:') else os.path.join(SRC, p), c) for p, c in ITEMS]
items = [(p, c) for p, c in items if os.path.exists(p)]
rows = (len(items) + cols - 1) // cols
sheet = Image.new('RGB', (cols * W, rows * (H + CAP)), (24, 24, 28))
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 21)
d = ImageDraw.Draw(sheet)
for i, (p, cap) in enumerate(items):
    im = Image.open(p).convert('RGB')
    r = max(W / im.width, H / im.height)
    im = im.resize((int(im.width * r + 0.5), int(im.height * r + 0.5)), Image.LANCZOS)
    x0, y0 = (im.width - W) // 2, (im.height - H) // 2
    im = im.crop((x0, y0, x0 + W, y0 + H))
    cx, cy = (i % cols) * W, (i // cols) * (H + CAP)
    sheet.paste(im, (cx, cy))
    d.text((cx + 14, cy + H + 8), cap, fill=(235, 235, 240), font=font)
sheet.save(OUT, quality=90)
print('写出', OUT, sheet.size, len(items), '张')
