"""11-02 机体零件来源摸底（只读，不保存）：blender -b 文件.blend -P tools/blender_mech_survey.py -- 输出.json
用户：「能不能保留大体形状的情况下，去除这些组件」。先看每个 .blend 里的零件哪些像组件库 / 素材包来的：
链接 / 追加过的库路径（bpy.data.libraries）、带资产元数据的物体、物体 / 网格 / 材质名的前缀、面数占比、集合结构。"""
import bpy
import collections
import json
import re
import sys

out = sys.argv[sys.argv.index('--') + 1]


def pref(n):
    n = re.sub(r'\.\d{3}$', '', n)
    m = re.match(r'([^\d_.\s#]+)', n)
    return m.group(1) if m else n[:8]


tris = collections.Counter()
objn = collections.Counter()
meshn = collections.Counter()
matn = collections.Counter()
asset_objs = []
total = 0
for o in bpy.data.objects:
    if o.type != 'MESH' or not o.data:
        continue
    t = sum(len(p.vertices) - 2 for p in o.data.polygons)
    total += t
    k = pref(o.name)
    objn[k] += 1
    tris[k] += t
    meshn[pref(o.data.name)] += 1
    for m in o.data.materials:
        if m:
            matn[pref(m.name)] += 1
    if o.asset_data or (o.data and getattr(o.data, 'asset_data', None)):
        asset_objs.append(o.name)
info = {
    'file': bpy.data.filepath,
    'libraries': [l.filepath for l in bpy.data.libraries],
    'n_mesh_objects': sum(objn.values()), 'total_tris': total,
    'object_prefixes': [(k, objn[k], tris[k]) for k, _ in tris.most_common(25)],
    'mesh_prefixes': meshn.most_common(15),
    'material_prefixes': matn.most_common(15),
    'asset_objects': asset_objs[:30], 'n_asset_objects': len(asset_objs),
    'collections': [(c.name, len(c.objects)) for c in bpy.data.collections][:30],
    'custom_props': sorted({k for o in bpy.data.objects for k in o.keys() if not k.startswith('_')})[:30],
}
json.dump(info, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
