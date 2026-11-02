"""K005 楼的法线体检（城区灰块，10-31）：每栋楼（Collection N）的每个网格物体，按面积统计面法线朝外（背离楼的竖直中轴）的比例。
外立面框、窗框、屋顶这类件应该大部分朝外；朝外比例很低的件 = 法线反了（UE 里材质单面，从外面看不见，只看见后面的砖墙壳 → 一整块灰）。
blender -b "Inbox/K005——摩天大楼建筑物城市街道楼房.blend" -P tools/blender_k005_normals.py -- 输出.json [编号,编号...]
"""
import json
import re
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = argv[0] if argv else 'k005_normals.json'
PICK = [int(x) for x in argv[1].split(',')] if len(argv) > 1 else None
deps = bpy.context.evaluated_depsgraph_get()
res = {}
for c in bpy.data.collections:
    m = re.fullmatch(r'Collection (\d+)', c.name)
    if not m or (PICK and int(m.group(1)) not in PICK):
        continue
    objs = [o for o in c.all_objects if o.type in ('MESH', 'CURVE') and not o.hide_render]
    if not objs:
        continue
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in objs:
        for v in o.bound_box:
            w = o.matrix_world @ Vector(v)
            lo = Vector((min(lo[i], w[i]) for i in range(3)))
            hi = Vector((max(hi[i], w[i]) for i in range(3)))
    axis = (lo + hi) / 2
    rows = []
    for o in objs:
        me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
        me.transform(o.matrix_world)
        neg = o.matrix_world.determinant() < 0
        if neg:
            me.flip_normals()   # 和 blender_k059_export.py 一样处理负缩放
        out = inn = up = down = 0.0
        for p in me.polygons:
            a = p.area
            n = p.normal
            if abs(n.z) > 0.7:     # 水平面：屋顶 / 楼板，朝上算朝外
                if n.z > 0:
                    up += a
                else:
                    down += a
                continue
            d = Vector((p.center.x - axis.x, p.center.y - axis.y, 0))
            if d.length < 1e-4:
                continue
            if n.dot(d) >= 0:
                out += a
            else:
                inn += a
        side = out + inn
        rows.append({'obj': o.name, 'mats': [s.material.name if s.material else None for s in o.material_slots], 'neg': neg,
                     'tris': sum(len(p.vertices) - 2 for p in me.polygons),
                     'out_ratio': round(out / side, 3) if side > 0 else None, 'side_area': round(side, 1),
                     'up_ratio': round(up / (up + down), 3) if up + down > 0 else None, 'flat_area': round(up + down, 1)})
        bpy.data.meshes.remove(me)
    res[c.name] = rows
    bad = [r for r in rows if r['out_ratio'] is not None and r['out_ratio'] < 0.4 and r['side_area'] > 5 and 'Interior' not in r['obj']]
    print(f'[normals] {c.name}: {len(rows)} 件，朝外比例 < 0.4 的 {len(bad)} 件 ' +
          ', '.join(f'{r["obj"]}({r["out_ratio"]}, 负缩放 {r["neg"]})' for r in bad[:8]))
json.dump(res, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[normals] 写好 {OUT}')
