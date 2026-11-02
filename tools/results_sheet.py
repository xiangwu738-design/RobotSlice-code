"""结算场景截图总览：把 Saved/Screenshots/Results 里四种模式（+ 平局、自动对战、联机客户端）的关键几帧拼成一张图 docs/结算场景_截图总览.jpg。
用法：py -3 tools/results_sheet.py"""
import os

from PIL import Image, ImageDraw, ImageFont

SRC = 'D:/RobotSlice/Saved/Screenshots/Results'
OUT = 'D:/RobotSlice/docs/结算场景_截图总览.jpg'
ROWS = [
    ('团队淘汰（3v3，你在胜方）', ['Elim_01', 'Elim_04', 'Elim_07', 'Elim_08']),
    ('团队死斗（你在败方）', ['DMTeam_02', 'DMTeam_05', 'DMTeam_07', 'DMTeam_08']),
    ('个人死斗（8 人，你第二）', ['DMSolo_02', 'DMSolo_04', 'DMSolo_07', 'DMSolo_08']),
    ('夺芯', ['Core_01', 'Core_05', 'Core_07', 'Core_08']),
    ('平局 / 自动对战真实数据 / 联机客户端', ['draw/Elim_07', 'auto_L_Street_Elim_04', 'client_auto_L_Street_Elim_02', 'client_auto_L_Street_Elim_04']),
]
W, H = 640, 360
PAD, LABEL = 8, 34
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 20)
sheet = Image.new('RGB', (PAD + 4 * (W + PAD), PAD + len(ROWS) * (H + LABEL + PAD)), (12, 14, 18))
d = ImageDraw.Draw(sheet)
for r, (title, names) in enumerate(ROWS):
    y = PAD + r * (H + LABEL + PAD)
    d.text((PAD, y + 4), title, fill=(230, 230, 225), font=font)
    for c, n in enumerate(names):
        p = os.path.join(SRC, n + '.png')
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert('RGB').resize((W, H), Image.LANCZOS)
        sheet.paste(im, (PAD + c * (W + PAD), y + LABEL))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
sheet.save(OUT, quality=90)
print(OUT, sheet.size)
