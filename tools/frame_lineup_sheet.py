"""把 -MechFrameLineup 的截图（Saved/Screenshots/Frames）拼成一张总览：上面整排正面 / 斜侧两张，下面九台各一列（正面、斜侧）。
可选第二组目录（-MechViewerShots=frames 的动作截图，Saved/Screenshots/FrameActions）拼成另一张「动作」总览。
  py -3 tools/frame_lineup_sheet.py [截图目录] [输出.jpg] [动作截图目录 动作输出.jpg]
默认：Saved/Screenshots/Frames → docs/阵营机体_游戏内总览.jpg
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'Saved', 'Screenshots', 'Frames')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'docs', '阵营机体_游戏内总览.jpg')
ACT_SRC = sys.argv[3] if len(sys.argv) > 3 else None
ACT_OUT = sys.argv[4] if len(sys.argv) > 4 else None

# 显示顺序 = 游戏里框架卡片的顺序（MechLoadout::DisplayOrder）：每家 轻 / 中 / 重
UNITS = [
    ('NEC_L', '新纪建设', '轻型', 'NEC-03「穿梭」', '原 v3'),
    ('NEC_M', '新纪建设', '中型', 'NEC-07「主力」', '挖掘机'),
    ('NEC_H', '新纪建设', '重型', 'NEC-15「承重」', '矿卡'),
    ('VHI_L', '维克托重工', '轻型', 'VHI-F33「斥候」', '草案名'),
    ('VHI_M', '维克托重工', '中型', 'VHI-F41「铁壁」', '草案名'),
    ('VHI_H', '维克托重工', '重型', 'VHI-F51「突击」', '草案名'),
    ('PHASE_L', '相位电子', '轻型', 'PHASE-F1「双塔」', '草案名'),
    ('PHASE_M', '相位电子', '中型', 'PHASE-F2「单塔」', '草案名'),
    ('PHASE_H', '相位电子', '重型', 'PHASE-F3「门架」', '草案名'),
]
FONT = 'C:/Windows/Fonts/msyh.ttc'
BG = (18, 20, 23)
INK = (232, 234, 236)
DIM = (150, 156, 162)
MAKER_COLOR = {'新纪建设': (238, 138, 28), '维克托重工': (194, 58, 46), '相位电子': (60, 200, 220)}


def font(size, bold=False):
    try:
        return ImageFont.truetype(FONT, size, index=1 if bold else 0)
    except OSError:
        return ImageFont.load_default()


def find(src, key, view):
    hits = sorted(glob.glob(os.path.join(src, f'*_{key}_{view}.png')))
    return hits[0] if hits else None


def crop_center(im, w_frac, h_frac=1.0, y_bias=0.0):
    W, H = im.size
    w, h = int(W * w_frac), int(H * h_frac)
    x0 = (W - w) // 2
    y0 = max(0, min(H - h, (H - h) // 2 + int(y_bias * H)))
    return im.crop((x0, y0, x0 + w, y0 + h))


def lineup_sheet():
    cell_w, cell_h = 330, 540
    head_h = 92
    cols = len(UNITS)
    top = []
    for name in ('00_整排_正面', '00_整排_斜侧'):
        p = os.path.join(SRC, name + '.png')
        if os.path.exists(p):
            top.append(Image.open(p).convert('RGB'))
    width = cell_w * cols
    top_h = 0
    top_imgs = []
    if top:
        tw = width // len(top)
        for im in top:
            im = crop_center(im, 1.0, 0.62, 0.04)
            im = im.resize((tw, int(im.size[1] * tw / im.size[0])), Image.LANCZOS)
            top_imgs.append(im)
        top_h = max(i.size[1] for i in top_imgs)
    title_h = 70
    sheet = Image.new('RGB', (width, title_h + top_h + head_h + cell_h * 2 + 30), BG)
    d = ImageDraw.Draw(sheet)
    d.text((24, 18), 'HX-07 阵营机体 · 游戏内（三家企业 × 轻 / 中 / 重，玩家涂装，测试场白天）', font=font(30, True), fill=INK)
    x = 0
    for im in top_imgs:
        sheet.paste(im, (x, title_h))
        x += im.size[0]
    y0 = title_h + top_h
    for c, (key, maker, tier, model, note) in enumerate(UNITS):
        x = c * cell_w
        col = MAKER_COLOR.get(maker, INK)
        d.rectangle((x + 10, y0 + 12, x + 16, y0 + 80), fill=col)
        d.text((x + 26, y0 + 8), f'{maker} {tier}', font=font(24, True), fill=INK)
        d.text((x + 26, y0 + 42), model, font=font(19), fill=DIM)
        d.text((x + 26, y0 + 66), f'{key} · {note}', font=font(15), fill=DIM)
        for r, view in enumerate(('正面', '斜侧')):
            p = find(SRC, key, view)
            if not p:
                d.text((x + 20, y0 + head_h + r * cell_h + 20), f'缺 {view}', font=font(18), fill=(200, 80, 80))
                continue
            im = crop_center(Image.open(p).convert('RGB'), 0.36, 1.0)
            im = im.resize((cell_w - 6, int(im.size[1] * (cell_w - 6) / im.size[0])), Image.LANCZOS)
            if im.size[1] > cell_h - 6:
                im = im.crop((0, (im.size[1] - (cell_h - 6)) // 2, im.size[0], (im.size[1] - (cell_h - 6)) // 2 + cell_h - 6))
            sheet.paste(im, (x + 3, y0 + head_h + r * cell_h + 3))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT, quality=90)
    print('写好', OUT, sheet.size)


def action_sheet():
    if not ACT_SRC or not ACT_OUT:
        return
    poses = ['开火_右前', '挥刀_右前', '飞行_右后', '重斩_左前上身']
    cell_w, cell_h = 300, 300
    head_w = 210
    sheet = Image.new('RGB', (head_w + cell_w * len(poses), 60 + cell_h * len(UNITS)), BG)
    d = ImageDraw.Draw(sheet)
    for i, pose in enumerate(poses):
        d.text((head_w + i * cell_w + 12, 16), pose.replace('_', ' '), font=font(22, True), fill=INK)
    for r, (key, maker, tier, model, note) in enumerate(UNITS):
        y = 60 + r * cell_h
        d.rectangle((10, y + 10, 16, y + 70), fill=MAKER_COLOR.get(maker, INK))
        d.text((26, y + 8), f'{maker}', font=font(20, True), fill=INK)
        d.text((26, y + 38), f'{tier} {model}', font=font(15), fill=DIM)
        for i, pose in enumerate(poses):
            p = find(ACT_SRC, key, pose)
            if not p:
                continue
            im = Image.open(p).convert('RGB')
            im = crop_center(im, 0.56, 1.0)
            s = min((cell_w - 6) / im.size[0], (cell_h - 6) / im.size[1])
            im = im.resize((int(im.size[0] * s), int(im.size[1] * s)), Image.LANCZOS)
            sheet.paste(im, (head_w + i * cell_w + 3 + (cell_w - 6 - im.size[0]) // 2, y + 3))
    sheet.save(ACT_OUT, quality=88)
    print('写好', ACT_OUT, sheet.size)


lineup_sheet()
action_sheet()
