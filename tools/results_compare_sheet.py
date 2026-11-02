"""结算场景改前改后对比图（10-31）：同一机位的改前 / 改后两张并排，几组拼成一张 docs/结算场景_改前改后_20261031.jpg。
改前、改后的截图各放一个目录（tools/run_results_test.sh、-MechFrameLineup、?MenuShots 截的，目录结构 Results/ Frames/ Menu/）。
用法：py -3 tools/results_compare_sheet.py <改前目录> <改后目录> [输出]"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

BEFORE, AFTER = sys.argv[1], sys.argv[2]
OUT = sys.argv[3] if len(sys.argv) > 3 else 'D:/RobotSlice/docs/结算场景_改前改后_20261031.jpg'
# (说明, 相对路径)：一行两组（改前 | 改后 ‖ 改前 | 改后）
PAIRS = [
    ('团队淘汰 MVP 特写（第 5.6 秒）：竖拇指不再拧身，聚光打亮', 'Results/Elim_04.png'),
    ('团队淘汰 正面全景（第 3.0 秒）：光柱，前排低姿端枪', 'Results/Elim_02.png'),
    ('团队淘汰 推近（第 4.4 秒）：后排败方枪口垂下', 'Results/Elim_03.png'),
    ('团队死斗 你在败方（第 6.9 秒）', 'Results/DMTeam_05.png'),
    ('个人死斗 领奖台（第 3.0 秒）', 'Results/DMSolo_02.png'),
    ('夺芯 MVP 特写（第 5.6 秒）', 'Results/Core_04.png'),
    ('九种框架 竖拇指（-MechFrameLineup 整排正面）', 'Frames/00_整排_正面.png'),
    ('维克托中型 竖拇指 正面', 'Frames/05_VHI_M_正面.png'),
    ('主菜单 竖拇指（?MenuShots）', 'Menu/1_主菜单.png'),
    ('结算表开着（第 11.2 秒）', 'Results/Elim_08.png'),
]
W, H = 640, 360
PAD, LABEL, TAG = 10, 30, 22
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 19)
small = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', 16)
cols = 2
rows = (len(PAIRS) + cols - 1) // cols
pair_w = 2 * W + PAD
sheet = Image.new('RGB', (PAD + cols * (pair_w + PAD * 3), PAD + rows * (H + LABEL + PAD * 2)), (12, 14, 18))
d = ImageDraw.Draw(sheet)
for i, (title, rel) in enumerate(PAIRS):
    r, c = divmod(i, cols)
    x0 = PAD + c * (pair_w + PAD * 3)
    y0 = PAD + r * (H + LABEL + PAD * 2)
    d.text((x0, y0 + 2), title, fill=(232, 232, 226), font=font)
    for k, (root, tag, col) in enumerate(((BEFORE, '改前', (200, 90, 80)), (AFTER, '改后', (90, 200, 150)))):
        p = os.path.join(root, rel)
        x = x0 + k * (W + PAD)
        if os.path.exists(p):
            sheet.paste(Image.open(p).convert('RGB').resize((W, H), Image.LANCZOS), (x, y0 + LABEL))
        else:
            d.rectangle((x, y0 + LABEL, x + W, y0 + LABEL + H), outline=(80, 80, 80))
            d.text((x + 12, y0 + LABEL + 12), '没有：' + rel, fill=(150, 150, 150), font=font)
        d.rectangle((x, y0 + LABEL, x + 54, y0 + LABEL + TAG + 4), fill=(0, 0, 0))
        d.text((x + 8, y0 + LABEL + 2), tag, fill=col, font=small)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
sheet.save(OUT, quality=88)
print(OUT, sheet.size)
