"""拼图：sheet.py 输出.jpg 列数 缩放 图1 图2 ...（每张图下面写文件名）"""
import sys
from PIL import Image, ImageDraw, ImageFont

out, cols, scale = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
files = sys.argv[4:]
ims = [Image.open(f).convert('RGB') for f in files]
w, h = int(ims[0].width * scale), int(ims[0].height * scale)
rows = (len(ims) + cols - 1) // cols
try:
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', max(14, int(h * 0.05)))
except OSError:
    font = ImageFont.load_default()
lab = int(h * 0.08)
sheet = Image.new('RGB', (cols * w, rows * (h + lab)), (24, 24, 24))
d = ImageDraw.Draw(sheet)
for i, (im, f) in enumerate(zip(ims, files)):
    x, y = (i % cols) * w, (i // cols) * (h + lab)
    sheet.paste(im.resize((w, h)), (x, y))
    d.text((x + 6, y + h + 2), f.replace('\\', '/').rsplit('/', 1)[-1].rsplit('.', 1)[0], fill=(230, 230, 230), font=font)
sheet.save(out, quality=88)
print('saved', out, sheet.size)
