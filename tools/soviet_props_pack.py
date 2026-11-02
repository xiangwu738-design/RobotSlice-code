"""苏联板楼：小品贴图整理成 UE 用的 T_<材质>_D / _N / _ARM，顺带把 Poly Haven 的雪地 / 路面贴图和地面遮罩也做好。
py -3 -I tools/soviet_props_pack.py Inbox/_soviet_props/export
输出 <目录>/ue_tex/*.png + materials_ue.json（格式同 k059_pack_textures.py）
"""
import json
import os
import re
import sys

from PIL import Image

SRC = os.path.abspath(sys.argv[1])
OUT = os.path.join(SRC, 'ue_tex')
os.makedirs(OUT, exist_ok=True)
Image.MAX_IMAGE_PIXELS = None
data = json.load(open(os.path.join(SRC, 'meshes.json'), encoding='utf-8'))


def clean(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s).strip('_')


def fit(path, mode, cap):
    im = Image.open(path)
    im = im.convert('RGB').convert('L') if mode == 'L' else im.convert(mode)
    w, h = im.size
    k = min(1.0, cap / max(w, h))
    # 边长拉成 2 的幂（压缩要 4 的倍数）
    def pot(v):
        p = 4
        while p * 2 <= v:
            p *= 2
        return p
    size = (pot(int(round(w * k))), pot(int(round(h * k))))
    return im.resize(size, Image.LANCZOS) if im.size != size else im


out = {}
for mname, e in data['materials'].items():
    key = clean(mname)
    ent = {'color': e.get('color', [0.8, 0.8, 0.8]), 'rough_v': e.get('rough_v', 0.6), 'metal_v': e.get('metal_v', 0.0), 'masked': bool(e.get('opacity')), 'tex': {}}
    if not e.get('base'):
        out[mname] = ent
        continue
    cap = e.get('cap', 1024)
    base = fit(e['base'], 'RGB', cap)
    size = base.size
    if e.get('opacity'):
        base.putalpha(fit(e['opacity'], 'L', cap).resize(size))
    base.save(os.path.join(OUT, f'T_{key}_D.png'))
    ent['tex']['D'] = f'T_{key}_D'
    if e.get('normal'):
        fit(e['normal'], 'RGB', cap).save(os.path.join(OUT, f'T_{key}_N.png'))
        ent['tex']['N'] = f'T_{key}_N'
    if e.get('rough') or e.get('gloss') or e.get('metal'):
        if e.get('rough'):
            r = fit(e['rough'], 'L', cap).resize(size)
        elif e.get('gloss'):
            r = fit(e['gloss'], 'L', cap).resize(size).point(lambda v: 255 - v)
        else:
            r = Image.new('L', size, int(ent['rough_v'] * 255))
        m = fit(e['metal'], 'L', cap).resize(size) if e.get('metal') else Image.new('L', size, int(ent['metal_v'] * 255))
        Image.merge('RGB', (Image.new('L', size, 255), r, m)).save(os.path.join(OUT, f'T_{key}_ARM.png'))
        ent['tex']['ARM'] = f'T_{key}_ARM'
    out[mname] = ent
    print('[svs-pack]', mname, size, sorted(ent['tex']), flush=True)

# Poly Haven 的地面贴图：雪、带雪沥青、远处雪原（4K → 2K）
TEX = 'D:/RobotSlice/Inbox/_polyhaven/textures'
for nm, d, n, r in (('Snow02', 'snow_02/snow_02_diff_4k.png', 'snow_02/snow_02_nor_gl_4k.png', 'snow_02/snow_02_rough_4k.png'),
                    ('AsphaltSnow', 'asphalt_snow/asphalt_snow_diff_4k.png', 'asphalt_snow/asphalt_snow_nor_gl_4k.png', 'asphalt_snow/asphalt_snow_rough_4k.png'),
                    ('SnowField', 'snow_field_aerial/snow_field_aerial_col_4k.png', 'snow_field_aerial/snow_field_aerial_nor_gl_4k.png', 'snow_field_aerial/snow_field_aerial_rough_4k.png')):
    if os.path.exists(os.path.join(OUT, f'T_G_{nm}_ARM.png')):
        continue
    base = fit(f'{TEX}/{d}', 'RGB', 2048)
    base.save(os.path.join(OUT, f'T_G_{nm}_D.png'))
    fit(f'{TEX}/{n}', 'RGB', 2048).save(os.path.join(OUT, f'T_G_{nm}_N.png'))
    rr = fit(f'{TEX}/{r}', 'L', 1024)
    Image.merge('RGB', (Image.new('L', rr.size, 255), rr, Image.new('L', rr.size, 0))).save(os.path.join(OUT, f'T_G_{nm}_ARM.png'))
    print('[svs-pack] 地面', nm, base.size, flush=True)
json.dump(out, open(os.path.join(SRC, 'materials_ue.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[svs-pack] 完成', len(out), '个材质，', len(os.listdir(OUT)), '张贴图', flush=True)
