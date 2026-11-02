"""把 anim_stick_dump.py 导出的关节坐标画成火柴人缩略图：每个动作一行，从左到右按时间，侧视（看机体右侧）。
右手、右腿红色，左手、左腿蓝色，手掌方向画一小段（看刀怎么挥）。
py -3 tools/anim_stick_sheet.py <json> <输出 png> [每格像素，默认 150]"""
import json
import sys

from PIL import Image, ImageDraw, ImageFont

data = json.load(open(sys.argv[1], encoding='utf-8'))
OUT = sys.argv[2]
CELL = int(sys.argv[3]) if len(sys.argv) > 3 else 150
LINKS = [('pelvis', 'spine_03', 'k'), ('spine_03', 'spine_05', 'k'), ('spine_05', 'neck_01', 'k'), ('neck_01', 'head', 'k'),
         ('spine_05', 'clavicle_l', 'l'), ('clavicle_l', 'upperarm_l', 'l'), ('upperarm_l', 'lowerarm_l', 'l'), ('lowerarm_l', 'hand_l', 'l'),
         ('hand_l', 'middle_metacarpal_l', 'l'),
         ('spine_05', 'clavicle_r', 'r'), ('clavicle_r', 'upperarm_r', 'r'), ('upperarm_r', 'lowerarm_r', 'r'), ('lowerarm_r', 'hand_r', 'r'),
         ('hand_r', 'middle_metacarpal_r', 'r'),
         ('pelvis', 'thigh_l', 'l'), ('thigh_l', 'calf_l', 'l'), ('calf_l', 'foot_l', 'l'), ('foot_l', 'ball_l', 'l'),
         ('pelvis', 'thigh_r', 'r'), ('thigh_r', 'calf_r', 'r'), ('calf_r', 'foot_r', 'r'), ('foot_r', 'ball_r', 'r')]
COLOR = {'k': (40, 40, 40), 'l': (40, 90, 230), 'r': (220, 40, 40)}
names = list(data)
ncol = max(len(v['frames']) for v in data.values())
LABEL = 300
W, H = LABEL + ncol * CELL, len(names) * CELL
img = Image.new('RGB', (W, H), 'white')
d = ImageDraw.Draw(img)
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 15)
# 统一比例：模板人形约 1.8 米高，格子里留余量（单位：Blender 里导入的长度，FBX 是厘米制时约 180）
allz = [p[2] for v in data.values() for fr in v['frames'] for p in fr.values()]
span = max(allz) - min(allz) if allz else 1.0
scale = CELL * 0.42 / max(span * 0.5, 1e-3)
for r, n in enumerate(names):
    v = data[n]
    y0 = r * CELL
    d.line([(0, y0), (W, y0)], fill=(220, 220, 220))
    d.text((6, y0 + 6), n, fill='black', font=font)
    d.text((6, y0 + 28), '%.2f 秒' % v['length'], fill=(110, 110, 110), font=font)
    for c, fr in enumerate(v['frames']):
        x0 = LABEL + c * CELL
        root = fr.get('pelvis', [0, 0, 0])
        # 侧视：横轴取前后（Blender 里导入后人形朝 -Y），竖轴 Z；以骨盆为中心
        def P(p):
            return (x0 + CELL / 2 + (-(p[1] - root[1])) * scale, y0 + CELL * 0.55 - (p[2] - root[2]) * scale)
        d.line([(x0 + 10, y0 + CELL * 0.55 + (root[2] - min(q[2] for q in fr.values())) * scale + 2)] * 2, fill='white')
        for a, b, col in LINKS:
            if a in fr and b in fr:
                d.line([P(fr[a]), P(fr[b])], fill=COLOR[col], width=3 if col == 'k' else 2)
        if 'head' in fr:
            hx, hy = P(fr['head'])
            d.ellipse([hx - 5, hy - 5, hx + 5, hy + 5], outline='black', width=2)
img.save(OUT)
print(OUT, img.size)
