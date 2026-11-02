"""开场运镜逐秒截图拼成一张对照表（10-31）：Saved/Screenshots/Intro/<键>/<键>_NN.png → Saved/Screenshots/Intro/<键>_一览.jpg
py -3 tools/intro_sheet.py <键> [每行几张=4]
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

key = sys.argv[1] if len(sys.argv) > 1 else 'street'
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 4
d = os.path.join('D:/RobotSlice/Saved/Screenshots/Intro', key)
files = sorted(glob.glob(os.path.join(d, f'{key}_[0-9][0-9].png'))) + sorted(glob.glob(os.path.join(d, f'{key}_fight.png')))
if not files:
    sys.exit(f'没有截图：{d}')
W = 480
ims = []
for f in files:
    im = Image.open(f).convert('RGB')
    im = im.resize((W, int(im.height * W / im.width)))
    ims.append((os.path.splitext(os.path.basename(f))[0].replace(key + '_', ''), im))
H = ims[0][1].height
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * W, rows * H), (20, 20, 20))
try:
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 22)
except OSError:
    font = ImageFont.load_default()
for i, (label, im) in enumerate(ims):
    x, y = (i % cols) * W, (i // cols) * H
    sheet.paste(im, (x, y))
    ImageDraw.Draw(sheet).text((x + 8, y + 6), label if label == 'fight' else f'{int(label)} 秒', fill=(255, 230, 80), font=font)
out = os.path.join('D:/RobotSlice/Saved/Screenshots/Intro', f'{key}_一览.jpg')
sheet.save(out, quality=88)
print(out, len(ims), '张')
