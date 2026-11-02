"""给侧视图画网格：grid.py 图... （x -30..60、z -22..23，每厘米 10 像素；每 5 厘米一条线，0 线红色）"""
import sys
from PIL import Image, ImageDraw, ImageFont

X0, Z1, PX = -30, 23, 10
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 14)
for f in sys.argv[1:]:
    im = Image.open(f).convert('RGB')
    d = ImageDraw.Draw(im)
    for x in range(-30, 61, 5):
        px = (x - X0) * PX
        d.line([(px, 0), (px, im.height)], fill=(255, 60, 60) if x == 0 else (90, 160, 255), width=1)
        d.text((px + 2, 2), str(x), fill=(20, 20, 160), font=font)
    for z in range(-20, 24, 5):
        pz = (Z1 - z) * PX
        d.line([(0, pz), (im.width, pz)], fill=(255, 60, 60) if z == 0 else (90, 160, 255), width=1)
        d.text((2, pz + 1), str(z), fill=(20, 20, 160), font=font)
    im.save(f)
