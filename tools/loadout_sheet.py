# 配装照片测试（-MechLoadoutShots）的截图拼成一张总览：上半选配整屏（draft_*），下半测试场配装页（range_*），每张下面写文件名。
# 用法：py -3 tools/loadout_sheet.py [输出路径]（默认 docs/配装界面_改后.jpg）
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ROOT / "Saved" / "Screenshots" / "Loadout"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "配装界面_改后.jpg"
CELL_W = 960  # 每张缩到这么宽
COLS = 4
LABEL_H = 34


def font(size):
    for name in ("msyh.ttc", "msyhbd.ttc", "simhei.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def group(prefix):
    return sorted(SHOTS.glob(f"{prefix}_*.png"))


def main():
    rows = []
    for prefix, title in (("draft", "选配整屏（-MechLobbyTest）"), ("range", "测试场配装页")):
        files = group(prefix)
        if files:
            rows.append((title, files))
    if not rows:
        print("没有截图：", SHOTS)
        return 1
    first = Image.open(rows[0][1][0])
    cell_h = round(first.height * CELL_W / first.width)
    title_h = 48
    total_h = 0
    for _, files in rows:
        n_rows = (len(files) + COLS - 1) // COLS
        total_h += title_h + n_rows * (cell_h + LABEL_H)
    sheet = Image.new("RGB", (COLS * CELL_W, total_h), (8, 10, 12))
    draw = ImageDraw.Draw(sheet)
    f_title, f_label = font(30), font(20)
    y = 0
    for title, files in rows:
        draw.text((16, y + 8), title, fill=(93, 202, 165), font=f_title)
        y += title_h
        for i, path in enumerate(files):
            img = Image.open(path).convert("RGB").resize((CELL_W, cell_h), Image.LANCZOS)
            x = (i % COLS) * CELL_W
            yy = y + (i // COLS) * (cell_h + LABEL_H)
            sheet.paste(img, (x, yy))
            draw.text((x + 10, yy + cell_h + 5), path.stem, fill=(200, 200, 196), font=f_label)
        y += ((len(files) + COLS - 1) // COLS) * (cell_h + LABEL_H)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT, quality=88)
    print("总览：", OUT, sheet.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
