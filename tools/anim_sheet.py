"""把 -MechAnimTest 的连拍拼成对照表：每行一个动作（4 帧连拍，刀那两段 8 帧），裁出机体附近。
py -3 tools/anim_sheet.py [截图目录] [输出前缀]"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SRC = sys.argv[1] if len(sys.argv) > 1 else 'D:/RobotSlice/Saved/Screenshots/Anim'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'D:/RobotSlice/Saved/Screenshots/动作对照'
CROP = 560
CELL = 300
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 22)

names = sorted({os.path.basename(p).rsplit('_', 1)[0] for p in glob.glob(f'{SRC}/*.png')})
for part in range(0, len(names), 5):
    rows = names[part:part + 5]
    frames = max(len(glob.glob(f'{SRC}/{n}_*.png')) for n in rows)
    frames = max(4, max(int(os.path.splitext(p)[0].rsplit('_', 1)[1]) + 1 for n in rows for p in glob.glob(f'{SRC}/{n}_*.png')))
    sheet = Image.new('RGB', (130 + CELL * frames, CELL * len(rows)), (24, 24, 26))
    d = ImageDraw.Draw(sheet)
    for r, n in enumerate(rows):
        d.text((8, r * CELL + CELL // 2 - 14), n.split('_', 1)[1], font=font, fill=(230, 230, 230))
        for k in range(frames):
            p = f'{SRC}/{n}_{k}.png'
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert('RGB')
            w, h = im.size
            cx, cy = w // 2, int(h * 0.52)
            im = im.crop((cx - CROP // 2, cy - CROP // 2, cx + CROP // 2, cy + CROP // 2)).resize((CELL, CELL), Image.LANCZOS)
            sheet.paste(im, (130 + k * CELL, r * CELL))
    path = f'{OUT}_{part // 5 + 1}.png'
    sheet.save(path)
    print(path)
