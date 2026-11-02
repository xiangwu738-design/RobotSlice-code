"""改前 / 改后对比图：py -3 -I tools/make_compare_sheet.py 改前目录 改后目录 输出.jpg [宽] [名字1 名字2 ...]
每行一个机位：左改前、右改后，底下写机位名。"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

a_dir, b_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
W = int(sys.argv[4]) if len(sys.argv) > 4 else 2400
names = sys.argv[5:] or sorted(f[:-4] for f in os.listdir(a_dir) if f.endswith('.png') and os.path.exists(os.path.join(b_dir, f)))
# 名字可以写成「机位=说明」，底下写说明（10-31 加）
labels = {n.split('=', 1)[0]: (n.split('=', 1)[1] if '=' in n else n.split('=', 1)[0]) for n in names}
names = [n.split('=', 1)[0] for n in names]
try:
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 26)
except Exception:
    font = ImageFont.load_default()
w = W // 2
first = Image.open(os.path.join(a_dir, names[0] + '.png'))
h = int(w * first.height / first.width)
lab = 40
sheet = Image.new('RGB', (W, (h + lab) * len(names) + lab), (18, 18, 18))
d = ImageDraw.Draw(sheet)
d.text((10, 6), '改前', fill=(240, 240, 240), font=font)
d.text((w + 10, 6), '改后', fill=(240, 240, 240), font=font)
for i, n in enumerate(names):
    y = lab + i * (h + lab)
    for j, folder in enumerate((a_dir, b_dir)):
        im = Image.open(os.path.join(folder, n + '.png')).convert('RGB').resize((w, h), Image.LANCZOS)
        sheet.paste(im, (j * w, y))
    d.text((10, y + h + 6), labels[n], fill=(220, 220, 220), font=font)
sheet.save(out, quality=90)
print(out, sheet.size)
