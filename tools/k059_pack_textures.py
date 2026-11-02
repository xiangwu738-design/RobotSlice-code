"""把 blender_k059_export.py 写出的原始贴图整理成 UE 用的几张（每个材质一套），并写 materials_ue.json：
T_<材质>_D（底色，有不透明度时放 alpha）、T_<材质>_N（OpenGL 法线；K005 只有高度图的现算）、T_<材质>_ARM（R=AO 1、G=粗糙度、B=金属度）、
T_<材质>_E（自发光）。同一张原图被多个材质共用时按内容去重（同一组输入只出一套）。
py -3 -I tools/k059_pack_textures.py Inbox/_k059/export_k059 1024 [材质名=边长 ...]
py -3 -I tools/k059_pack_textures.py Inbox/_k059/export_k005 2048
"""
import hashlib
import json
import os
import re
import sys

import numpy as np
from PIL import Image

SRC = os.path.abspath(sys.argv[1])
MAXS = int(sys.argv[2])
OVR = dict(a.split('=') for a in sys.argv[3:])
OUT = os.path.join(SRC, 'ue_tex')
os.makedirs(OUT, exist_ok=True)
data = json.load(open(os.path.join(SRC, 'meshes.json'), encoding='utf-8'))
Image.MAX_IMAGE_PIXELS = None


def clean(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s).strip('_')


def load(name, mode, size):
    im = Image.open(os.path.join(SRC, 'raw', name))
    if mode == 'L' and im.mode in ('RGB', 'RGBA', 'P'):
        im = im.convert('RGB').convert('L')
    else:
        im = im.convert(mode)
    if im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return im


def target(name, cap):
    w, h = Image.open(os.path.join(SRC, 'raw', name)).size
    k = min(1.0, cap / max(w, h))
    return max(4, int(round(w * k))), max(4, int(round(h * k)))


def height_to_normal(h, strength=3.0):
    a = np.asarray(h, dtype=np.float32) / 255.0
    dx = (np.roll(a, -1, 1) - np.roll(a, 1, 1)) * 0.5
    dy = (np.roll(a, -1, 0) - np.roll(a, 1, 0)) * 0.5
    nx, ny = -dx * strength * a.shape[1] / 256.0, dy * strength * a.shape[0] / 256.0
    nz = np.ones_like(a)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack(((nx / ln + 1) * 127.5, (ny / ln + 1) * 127.5, (nz / ln + 1) * 127.5), -1)
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB')


out = {}
done = {}
for mname, d in data['materials'].items():
    cap = int(OVR.get(mname, MAXS))
    key = clean(mname)
    ent = {'color': d['color'], 'rough_v': d['rough_v'], 'metal_v': d['metal_v'], 'masked': d['masked'], 'tex': {}}
    sig = hashlib.md5(json.dumps([d.get(k) for k in ('base', 'opacity', 'normal', 'height', 'rough', 'metal', 'emissive')]
                                 + [cap, d['metal_v']]).encode()).hexdigest()[:10]
    if not any(d.get(k) for k in ('base', 'normal', 'height', 'rough', 'emissive')):
        out[mname] = ent       # 纯色材质
        continue
    if sig in done:
        ent['tex'] = done[sig]
        out[mname] = ent
        continue
    ref = d.get('base') or d.get('rough') or d.get('normal') or d.get('height')
    size = target(ref, cap)
    tex = {}
    if d.get('base'):
        base = load(d['base'], 'RGB', size)
        if d.get('opacity'):
            base.putalpha(load(d['opacity'], 'L', size))
        base.save(os.path.join(OUT, f'T_{key}_D.png'))
        tex['D'] = f'T_{key}_D'
    if d.get('normal'):
        load(d['normal'], 'RGB', size).save(os.path.join(OUT, f'T_{key}_N.png'))
        tex['N'] = f'T_{key}_N'
    elif d.get('height'):
        height_to_normal(load(d['height'], 'L', size)).save(os.path.join(OUT, f'T_{key}_N.png'))
        tex['N'] = f'T_{key}_N'
    if d.get('rough') or d.get('metal'):
        r = load(d['rough'], 'L', size) if d.get('rough') else Image.new('L', size, int(d['rough_v'] * 255))
        m = load(d['metal'], 'L', size) if d.get('metal') else Image.new('L', size, int(d['metal_v'] * 255))
        Image.merge('RGB', (Image.new('L', size, 255), r, m)).save(os.path.join(OUT, f'T_{key}_ARM.png'))
        tex['ARM'] = f'T_{key}_ARM'
    if d.get('emissive'):
        e = load(d['emissive'], 'RGB', size)
        if max(e.getextrema()[c][1] for c in range(3)) > 8:
            e.save(os.path.join(OUT, f'T_{key}_E.png'))
            tex['E'] = f'T_{key}_E'
    done[sig] = tex
    ent['tex'] = tex
    out[mname] = ent
    print(f'[pack] {mname} {size} {sorted(tex)}', flush=True)
# 世界投影方块用的金属贴图（钢、车厢）：金属度压到 30%，阴天下大面金属只反射灰天空，一片发黑（K051 代理踩过）
for mname in ('KB3D_CTS_SteelRoughGray', 'KB3D_CTS_MetalPaintGrayA', 'KB3D_CTS_MetalPaintWhite', 'KB3D_CTS_SteelPaintBlackA'):
    t = out.get(mname, {}).get('tex', {}).get('ARM')
    if t:
        im = Image.open(os.path.join(OUT, t + '.png')).convert('RGB')
        r, g, b = im.split()
        b = b.point(lambda v: int(v * 0.3))
        Image.merge('RGB', (r, g, b)).save(os.path.join(OUT, t + 'W.png'))
        print('[pack] 世界投影低金属', t + 'W')
json.dump(out, open(os.path.join(SRC, 'materials_ue.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[pack] 材质 {len(out)}，贴图 {len(os.listdir(OUT))} 张 → {OUT}')
