# 手部重复 改前改后对比图：九台 × [竖大拇指 正面 / 竖大拇指 右前 / 握刀 右前] × [改前 | 改后]，下面再拼新纪轻型握持特写（可选）。
# py -3 tools/grip_lab/make_hand_dup_sheet.py 输出.jpg [握持特写改前目录 握持特写改后目录 图名1 图名2 ...]
import os
import sys

from PIL import Image, ImageDraw, ImageFont

D = 'D:/RobotSlice/Saved/Screenshots/FrameHands'
OUT = sys.argv[1]
EXTRA = sys.argv[2:]
KEYS = [('01_NEC_L', '新纪 轻（v3）'), ('02_NEC_M', '新纪 中'), ('03_NEC_H', '新纪 重'), ('04_VHI_L', '维克托 轻'), ('05_VHI_M', '维克托 中'),
        ('06_VHI_H', '维克托 重'), ('07_PHASE_L', '相位 轻'), ('08_PHASE_M', '相位 中'), ('09_PHASE_H', '相位 重')]
COLS = [('', '右手正面', '竖大拇指·正面'), ('', '右手右前', '竖大拇指·右前'), ('握刀_', '右手右前', '握刀·右前')]
CW, CH = 330, 248          # 每格
CROP = (960 - 560, 540 - 420, 960 + 560, 540 + 420)   # 镜头对准手：取中间 1120×840
LW = 180                   # 左边台名列宽
HH = 70                    # 表头高
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 26)
small = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 20)
title = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', 34)

W = LW + len(COLS) * 2 * CW + (len(COLS) - 1) * 16
rows = []
top = Image.new('RGB', (W, 60 + HH), (24, 26, 30))
dr = ImageDraw.Draw(top)
dr.text((16, 10), '手部重复（手指第二、三节和第一节脱开）改前 / 改后 · 九台同一机位 · 2026-10-31', fill=(240, 240, 240), font=title)
x = LW
for _, _, name in COLS:
    dr.text((x + 10, 66), name + '  改前', fill=(255, 150, 120), font=font)
    dr.text((x + CW + 10, 66), '改后', fill=(140, 230, 160), font=font)
    x += 2 * CW + 16
rows.append(top)
for key, cn in KEYS:
    row = Image.new('RGB', (W, CH), (24, 26, 30))
    ImageDraw.Draw(row).text((12, CH // 2 - 16), cn, fill=(235, 235, 235), font=font)
    x = LW
    for pre, view, _ in COLS:
        for i, tag in enumerate(('改前', '改后')):
            p = os.path.join(D, pre + tag, f'{key}_{view}.png')
            if os.path.exists(p):
                im = Image.open(p).convert('RGB').crop(CROP).resize((CW - 4, CH - 4), Image.LANCZOS)
                row.paste(im, (x + i * CW + 2, 2))
        x += 2 * CW + 16
    rows.append(row)
if EXTRA:
    bdir, adir, names = EXTRA[0], EXTRA[1], EXTRA[2:]
    hdr = Image.new('RGB', (W, 60), (24, 26, 30))
    ImageDraw.Draw(hdr).text((16, 12), '新纪轻型 模型查看 -MechViewerShots=hands 手部特写（同一帧、同一机位）：每对左改前、右改后', fill=(240, 240, 240), font=font)
    rows.append(hdr)
    per = 3                                   # 一行三对
    cw2 = (W - 16 * (per - 1)) // (2 * per)
    ch2 = cw2 * 9 // 16
    for r in range(0, len(names), per):
        row = Image.new('RGB', (W, ch2 + 34), (24, 26, 30))
        dr = ImageDraw.Draw(row)
        for j, n in enumerate(names[r:r + per]):
            x = j * (2 * cw2 + 16)
            for i, d in enumerate((bdir, adir)):
                p = os.path.join(d, n)
                if os.path.exists(p):
                    row.paste(Image.open(p).convert('RGB').crop((240, 135, 1040, 585)).resize((cw2 - 4, ch2 - 4), Image.LANCZOS), (x + i * cw2 + 2, 2))   # 镜头对准手骨：取中间 800×450
            dr.text((x + 6, ch2 + 2), os.path.splitext(n)[0][3:], fill=(220, 220, 220), font=small)
        rows.append(row)
H = sum(r.height for r in rows)
sheet = Image.new('RGB', (W, H), (24, 26, 30))
y = 0
for r in rows:
    sheet.paste(r, (0, y))
    y += r.height
sheet.save(OUT, quality=90)
print(OUT, sheet.size)
