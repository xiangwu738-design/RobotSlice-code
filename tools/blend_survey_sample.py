"""每个资产库文件均匀抽样拼一张小总览（省得一张张翻）：py -3 -I blend_survey_sample.py 预览目录 文件名[:张数] ..."""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont
root = sys.argv[1]
f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 12)
for arg in sys.argv[2:]:
    d, n = (arg.split(':') + ['40'])[:2]
    n = int(n)
    s = json.load(open(os.path.join(root, d, '_survey.json'), encoding='utf-8'))
    items = [it for it in s['items'] if it['preview']]
    pick = [items[round(k * (len(items) - 1) / max(1, n - 1))] for k in range(min(n, len(items)))] if len(items) > n else items
    files = {int(x[:4]): x for x in os.listdir(os.path.join(root, d)) if x.endswith('.png') and x[:4].isdigit()}
    cols, cell = 10, 140
    rows = (len(pick) + cols - 1) // cols
    o = Image.new('RGB', (cols * cell, rows * (cell + 30)), (40, 40, 44))
    dr = ImageDraw.Draw(o)
    for k, it in enumerate(pick):
        x, y = (k % cols) * cell, (k // cols) * (cell + 30)
        im = Image.open(os.path.join(root, d, files[it['i']])).convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM).resize((cell - 4, cell - 4))
        bg = Image.new('RGBA', im.size, (90, 90, 96, 255)); bg.alpha_composite(im)
        o.paste(bg.convert('RGB'), (x + 2, y + 2))
        dr.text((x + 3, y + cell - 1), f"{it['i']} {it['name'][:10]}", fill=(255, 230, 120), font=f)
        dr.text((x + 3, y + cell + 13), f"{it['cat'][:7]} {it['tris'] // 1000}k {max(it['size']):.1f}", fill=(200, 200, 200), font=f)
    o.save(os.path.join(root, f'抽样_{d}.jpg'), quality=85)
    print(d, len(items), '→', len(pick))
