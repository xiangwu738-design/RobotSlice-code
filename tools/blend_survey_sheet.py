"""把 blend_asset_survey.py 存下的缩略图拼成总览图（每格：编号、名字、分类、三角面）。py -3 -I blend_survey_sheet.py 预览目录 [每页格数]"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont
root = sys.argv[1]
per = int(sys.argv[2]) if len(sys.argv) > 2 else 48
f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 13)
for d in sorted(os.listdir(root)):
    p = os.path.join(root, d, '_survey.json')
    if not os.path.exists(p):
        continue
    s = json.load(open(p, encoding='utf-8'))
    items = [it for it in s['items'] if it['preview']]
    files = {int(n[:4]): n for n in os.listdir(os.path.join(root, d)) if n.endswith('.png') and n[:4].isdigit()}
    cols, cell = 8, 180
    for page in range((len(items) + per - 1) // per):
        chunk = items[page * per:(page + 1) * per]
        rows = (len(chunk) + cols - 1) // cols
        o = Image.new('RGB', (cols * cell, rows * (cell + 34)), (40, 40, 44))
        dr = ImageDraw.Draw(o)
        for k, it in enumerate(chunk):
            x, y = (k % cols) * cell, (k // cols) * (cell + 34)
            im = Image.open(os.path.join(root, d, files[it['i']])).convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM).resize((cell - 4, cell - 4))
            bg = Image.new('RGBA', im.size, (90, 90, 96, 255)); bg.alpha_composite(im)
            o.paste(bg.convert('RGB'), (x + 2, y + 2))
            dr.text((x + 4, y + cell - 2), f"{it['i']} {it['name'][:12]}", fill=(255, 230, 120), font=f)
            dr.text((x + 4, y + cell + 14), f"{it['cat'][:8]} {it['tris'] // 1000}k面", fill=(200, 200, 200), font=f)
        o.save(os.path.join(root, f'{d}_{page + 1}.jpg'), quality=85)
    print(d, len(s['items']), '个资产', len(items), '有缩略图', '贴图', s['images'], '缺', s['images_missing'])
