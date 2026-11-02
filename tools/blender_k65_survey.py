"""月球基地：看 K65（KB3D Lunar Base）的结构。只读，不存 .blend。
blender -b Inbox/K65_月球基地.blend -P tools/blender_k65_survey.py -- 输出目录
写 survey.json：每个 Export 集合 → 名字、物体、包围盒、面数、材质；材质 → 贴图文件名（是否内嵌）；打印摘要。
"""
import json
import os
import sys

import bpy
from mathutils import Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(OUT, exist_ok=True)
print('[survey] scenes', [(s.name, len(s.objects)) for s in bpy.data.scenes])


def info(o):
    lo, hi = Vector((1e12,) * 3), Vector((-1e12,) * 3)
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        for i in range(3):
            lo[i], hi[i] = min(lo[i], w[i]), max(hi[i], w[i])
    return lo, hi


groups = []
for c in bpy.data.collections:
    obs = [o for o in c.objects if o.type == 'MESH']
    if not obs:
        continue
    rows = []
    for o in sorted(obs, key=lambda o: o.name):
        lo, hi = info(o)
        rows.append({'name': o.name, 'min': [round(v, 2) for v in lo], 'max': [round(v, 2) for v in hi], 'polys': len(o.data.polygons),
                     'mats': [s.material.name for s in o.material_slots if s.material], 'det': round(o.matrix_world.determinant(), 3),
                     'mods': [m.type for m in o.modifiers]})
    groups.append({'collection': c.name, 'objects': rows})

mats = {}
for m in bpy.data.materials:
    tex = {}
    P = None
    if m.use_nodes and m.node_tree:
        for n in m.node_tree.nodes:
            if n.bl_idname == 'ShaderNodeBsdfPrincipled':
                P = n
            if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                tex[n.name] = {'image': n.image.name, 'file': os.path.basename(n.image.filepath), 'packed': bool(n.image.packed_file), 'size': list(n.image.size)}
    mats[m.name] = {'tex': tex, 'alpha': (P.inputs['Alpha'].default_value if P else None), 'links': ({i.name: i.links[0].from_node.name for i in P.inputs if i.is_linked} if P else {})}

json.dump({'groups': groups, 'materials': mats}, open(f'{OUT}/survey.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[survey] 集合', len(groups), '材质', len(mats), '→', OUT)
for g in groups:
    t = sum(o['polys'] for o in g['objects'])
    print(g['collection'], len(g['objects']), t, g['objects'][0]['name'])
