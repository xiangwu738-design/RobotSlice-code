# 拼总览：py -3 make_sheet.py 输出.jpg 列数 单图宽 标题=文件 标题=文件 ...
import os
import sys
from PIL import Image, ImageDraw, ImageFont

out, cols, w = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
items = [a.split('=', 1) for a in sys.argv[4:]]
items = [(t, f) for t, f in items if os.path.exists(f)]
h = w * 9 // 16
bar = 34
rows = (len(items) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w + (cols + 1) * 6, rows * (h + bar) + (rows + 1) * 6), (20, 22, 26))
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 20)
d = ImageDraw.Draw(sheet)
for i, (t, f) in enumerate(items):
    x, y = 6 + (i % cols) * (w + 6), 6 + (i // cols) * (h + bar + 6)
    im = Image.open(f).convert('RGB')
    im.thumbnail((w, h))
    sheet.paste(im, (x + (w - im.width) // 2, y + bar + (h - im.height) // 2))
    d.text((x + 6, y + 4), t, fill=(235, 235, 235), font=font)
sheet.save(out, quality=86)
print(out, sheet.size, round(os.path.getsize(out) / 1024 / 1024, 2), 'MB')
