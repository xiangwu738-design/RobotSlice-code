"""握持改前改后对比拼图：py -3 make_grip_sheet.py <改前目录> <改后目录> <输出.jpg> [裁中间的比例 0.62]
-MechViewerShots=hands 的截图按编号对上（01_双手刀_待机_右手.png …），每把武器一行：待机右手、待机左手、出招 / 开火右手、左手，各「改前 | 改后」。"""
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

before, after, out = sys.argv[1], sys.argv[2], sys.argv[3]
keep = float(sys.argv[4]) if len(sys.argv) > 4 else 0.62
pat = re.compile(r'(\d+)_(.+)_(待机|出招|开火)_(右手|左手)\.png$')


def index(d):
    m = {}
    for f in os.listdir(d):
        r = pat.match(f)
        if r:
            m[int(r.group(1))] = (r.group(2), r.group(3), r.group(4), os.path.join(d, f))
    return m


B, A = index(before), index(after)
nums = sorted(set(B) & set(A))
rows = []
for n in nums:
    w = B[n][0]
    if not rows or rows[-1][0] != w:
        rows.append((w, []))
    rows[-1][1].append(n)
CW, CH = 320, 186
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 18)
small = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 15)
cols = 8
head = 40
sheet = Image.new('RGB', (110 + cols * CW, head + len(rows) * (CH + 6)), (20, 21, 23))
d = ImageDraw.Draw(sheet)
labels = ['待机·右手', '待机·左手', '出招/开火·右手', '出招/开火·左手']
for i, t in enumerate(labels):
    d.text((110 + i * 2 * CW + 8, 10), t + '（左：改前　右：改后）', fill=(220, 220, 220), font=font)


def crop(path):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    cw, ch = int(w * keep), int(h * keep)
    im = im.crop(((w - cw) // 2, (h - ch) // 2, (w + cw) // 2, (h + ch) // 2))
    im.thumbnail((CW - 4, CH - 4))
    return im


for r, (w, ns) in enumerate(rows):
    y = head + r * (CH + 6)
    d.text((8, y + CH // 2 - 12), w, fill=(240, 240, 240), font=font)
    for c, n in enumerate(ns[:4]):
        for k, src in enumerate((B, A)):
            im = crop(src[n][3])
            x = 110 + (c * 2 + k) * CW
            sheet.paste(im, (x + 2, y + 2))
            d.text((x + 6, y + 4), '改前' if k == 0 else '改后', fill=(255, 210, 120) if k == 0 else (140, 230, 255), font=small)
sheet.save(out, quality=90)
print('saved', out, sheet.size, '行', len(rows))
