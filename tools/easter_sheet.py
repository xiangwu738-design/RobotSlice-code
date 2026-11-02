"""彩蛋模型拼图（11-02）：把 Blender 脚本拍的图（目录里的 _sheet.json 列了文件和标题、图例）拼成一张 jpg，发回来看这一张就行。
  py -3 tools/easter_sheet.py Inbox/_easter/preview/dog          → Inbox/_easter/preview/dog_总览.jpg
  py -3 tools/easter_sheet.py Inbox/_easter/survey/drone [列数] [格宽] [输出.jpg]
没有 _sheet.json 就按文件名排所有 png。只用 PIL。
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def font(size):
    for f in ('C:/Windows/Fonts/msyh.ttc', 'C:/Windows/Fonts/simhei.ttf', '/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc',
              '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src = sys.argv[1] if os.path.isabs(sys.argv[1]) else os.path.join(ROOT, sys.argv[1])
    src = os.path.normpath(src)
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 640
    out = sys.argv[4] if len(sys.argv) > 4 else src.rstrip('/\\') + '_总览.jpg'
    meta = {'title': os.path.basename(src), 'items': [], 'legend': [], 'extra': []}
    mp = os.path.join(src, '_sheet.json')
    if os.path.exists(mp):
        meta.update(json.load(open(mp, encoding='utf-8')))
    items = [(it['title'], os.path.join(src, it['file'])) for it in meta['items']]
    if not items:
        items = [(os.path.splitext(f)[0], os.path.join(src, f)) for f in sorted(os.listdir(src)) if f.lower().endswith('.png')]
    missing = [f for _t, f in items if not os.path.exists(f)]
    items = [(t, f) for t, f in items if os.path.exists(f)]
    if not items:
        print('没有图：', src)
        sys.exit(1)
    h = w * 3 // 4
    bar, pad = 30, 6
    f_title, f_cell, f_small = font(26), font(17), font(15)
    legend = meta.get('legend') or []
    extra = meta.get('extra') or []
    head = 44 + (26 if extra else 0) + ((len(legend) + 5) // 6) * 24
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w + (cols + 1) * pad, head + rows * (h + bar + pad) + pad), (20, 22, 26))
    d = ImageDraw.Draw(sheet)
    d.text((pad + 4, 8), meta.get('title', ''), fill=(240, 240, 240), font=f_title)
    y = 44
    if extra:
        d.text((pad + 4, y), '   '.join(str(e) for e in extra), fill=(200, 200, 200), font=f_small)
        y += 26
    for i, lg in enumerate(legend):
        x = pad + 4 + (i % 6) * (sheet.width // 6)
        yy = y + (i // 6) * 24
        d.rectangle((x, yy + 3, x + 16, yy + 19), fill=tuple(lg['rgb']))
        d.text((x + 22, yy + 1), str(lg['label'])[:26], fill=(220, 220, 220), font=f_small)
    for i, (t, f) in enumerate(items):
        x = pad + (i % cols) * (w + pad)
        yy = head + (i // cols) * (h + bar + pad)
        im = Image.open(f).convert('RGB')
        im.thumbnail((w, h))
        sheet.paste(im, (x + (w - im.width) // 2, yy + bar + (h - im.height) // 2))
        d.text((x + 4, yy + 5), t[:60], fill=(235, 235, 235), font=f_cell)
    sheet.save(out, quality=86)
    print(out, sheet.size, round(os.path.getsize(out) / 1024 / 1024, 2), 'MB', f'（{len(items)} 张）')
    if missing:
        print('清单里有但找不到的图：', missing)


if __name__ == '__main__':
    main()
