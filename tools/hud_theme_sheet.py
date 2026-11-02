# HUD 主题对比图（10-31）：把 -MechHudThemeShots 的截图拼成对比表。
# 用法：py -3.11 tools/hud_theme_sheet.py
# 输入：Saved/Screenshots/HudTheme/<day|night>/<序号>_<状态>_<STD|NEC|VHI|PHASE>.png
# 输出：docs/HUD主题_对比.jpg（白天，四列主题 × 五行状态）
#      docs/HUD主题_白天.jpg / docs/HUD主题_夜间.jpg（交战中，四套 2×2 大图）
#      docs/HUD主题_仪表盘特写.jpg（交战中，准星下方细条 + 底部仪表盘裁出来，四套上下排）
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, "Saved", "Screenshots", "HudTheme")
DOCS = os.path.join(ROOT, "docs")
THEMES = [("STD", "联赛标准"), ("NEC", "新纪建设"), ("VHI", "维克托重工"), ("PHASE", "相位电子")]
FONT = "C:/Windows/Fonts/msyh.ttc"
FONT_B = "C:/Windows/Fonts/msyhbd.ttc"


def load(tag):
    """{(序号, 状态): {主题: 路径}}"""
    out = {}
    for p in glob.glob(os.path.join(SHOTS, tag, "*.png")):
        name = os.path.splitext(os.path.basename(p))[0]
        parts = name.split("_")
        if len(parts) < 3:
            continue
        idx, state, code = parts[0], "_".join(parts[1:-1]), parts[-1]
        out.setdefault((int(idx), state), {})[code] = p
    return dict(sorted(out.items()))


def font(size, bold=False):
    return ImageFont.truetype(FONT_B if bold else FONT, size)


def grid(rows, cell_w, title, out_path):
    cell_h = cell_w * 9 // 16
    left = 150
    top = 110
    pad = 8
    w = left + len(THEMES) * (cell_w + pad)
    h = top + len(rows) * (cell_h + pad) + 20
    sheet = Image.new("RGB", (w, h), (18, 19, 21))
    d = ImageDraw.Draw(sheet)
    d.text((24, 20), title, font=font(34, True), fill=(235, 235, 230))
    for c, (code, name) in enumerate(THEMES):
        x = left + c * (cell_w + pad)
        d.text((x + 6, top - 50), f"{name}  {code}", font=font(26, True), fill=(225, 225, 220))
    for r, (label, shots) in enumerate(rows):
        y = top + r * (cell_h + pad)
        d.text((20, y + cell_h // 2 - 18), label, font=font(24, True), fill=(210, 210, 205))
        for c, (code, _) in enumerate(THEMES):
            x = left + c * (cell_w + pad)
            p = shots.get(code)
            if not p:
                d.rectangle([x, y, x + cell_w, y + cell_h], outline=(90, 40, 40), width=2)
                d.text((x + 20, y + 20), "缺图", font=font(22), fill=(200, 80, 80))
                continue
            im = Image.open(p).convert("RGB").resize((cell_w, cell_h), Image.LANCZOS)
            sheet.paste(im, (x, y))
    sheet.save(out_path, quality=90)
    print("写出", out_path, sheet.size)


def quad(shots, title, out_path, cell_w=1280):
    cell_h = cell_w * 9 // 16
    pad = 10
    top = 70
    w = 2 * cell_w + pad
    h = top + 2 * (cell_h + pad + 40)
    sheet = Image.new("RGB", (w, h), (18, 19, 21))
    d = ImageDraw.Draw(sheet)
    d.text((20, 16), title, font=font(32, True), fill=(235, 235, 230))
    for i, (code, name) in enumerate(THEMES):
        x = (i % 2) * (cell_w + pad)
        y = top + (i // 2) * (cell_h + pad + 40)
        d.text((x + 6, y), f"{name}  {code}", font=font(24, True), fill=(225, 225, 220))
        p = shots.get(code)
        if p:
            sheet.paste(Image.open(p).convert("RGB").resize((cell_w, cell_h), Image.LANCZOS), (x, y + 36))
    sheet.save(out_path, quality=90)
    print("写出", out_path, sheet.size)


def dash_strip(shots, out_path):
    # 1920×1080 原图：准星下方细条在 0.73H 一带，仪表盘在最下面 130 像素；左右各裁到仪表盘两端外一点
    pieces = []
    for code, name in THEMES:
        p = shots.get(code)
        if not p:
            continue
        im = Image.open(p).convert("RGB")
        W, H = im.size
        k = H / 1080.0
        x0, x1 = int(W / 2 - 600 * k), int(W / 2 + 600 * k)
        strip = im.crop((x0, int(H * 0.73 - 40 * k), x1, int(H * 0.73 + 30 * k)))
        dash = im.crop((x0, int(H - 150 * k), x1, H))
        pieces.append((name, code, strip, dash))
    if not pieces:
        return
    cw = pieces[0][2].width
    label_h = 40
    h = sum(label_h + p[2].height + 6 + p[3].height + 16 for p in pieces) + 20
    sheet = Image.new("RGB", (cw, h), (18, 19, 21))
    d = ImageDraw.Draw(sheet)
    y = 10
    for name, code, strip, dash in pieces:
        d.text((10, y), f"{name}  {code}", font=font(24, True), fill=(225, 225, 220))
        y += label_h
        sheet.paste(strip, (0, y))
        y += strip.height + 6
        sheet.paste(dash, (0, y))
        y += dash.height + 16
    sheet.save(out_path, quality=92)
    print("写出", out_path, sheet.size)


def main():
    day = load("day")
    night = load("night")
    if not day:
        print("没有白天的截图：", os.path.join(SHOTS, "day"))
        sys.exit(1)
    rows = [(state, shots) for (_, state), shots in day.items()]
    grid(rows, 960, "HUD 主题对比（测试场 · 白天 13:00 · 同一状态下四套主题）", os.path.join(DOCS, "HUD主题_对比.jpg"))
    engage_day = next((s for (i, st), s in day.items() if i == 0), None)
    if engage_day:
        quad(engage_day, "HUD 主题 · 白天（测试场 13:00，交战中）", os.path.join(DOCS, "HUD主题_白天.jpg"))
        dash_strip(engage_day, os.path.join(DOCS, "HUD主题_仪表盘特写.jpg"))
    engage_night = next((s for (i, st), s in night.items() if i == 0), None)
    if engage_night:
        quad(engage_night, "HUD 主题 · 夜间（测试场 22:00，交战中）", os.path.join(DOCS, "HUD主题_夜间.jpg"))
    if night:
        rows_n = [(state, shots) for (_, state), shots in night.items()]
        grid(rows_n, 640, "HUD 主题对比（测试场 · 夜间 22:00）", os.path.join(SHOTS, "夜间_全部状态.jpg"))


if __name__ == "__main__":
    main()
