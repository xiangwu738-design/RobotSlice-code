"""K059 城市街道包普查（只读，不改 .blend）：场景 / 集合 / 物体 / 实例 / 材质 / 贴图统计，写 survey.json。
blender -b "Inbox/K059 _城市街道.blend" -P tools/blender_k059_survey.py -- Inbox/_k059/survey.json
"""
import json
import os
import sys
from collections import Counter, defaultdict

import bpy
from mathutils import Vector

out = sys.argv[sys.argv.index('--') + 1]
sc = bpy.context.scene
print('[k059] 场景', [s.name for s in bpy.data.scenes], '当前', sc.name, '单位', sc.unit_settings.system, sc.unit_settings.scale_length)
types = Counter(o.type for o in sc.objects)
print('[k059] 物体类型', dict(types))
inst = [o for o in sc.objects if o.type == 'EMPTY' and o.instance_type == 'COLLECTION' and o.instance_collection]
print('[k059] 集合实例', len(inst), Counter(o.instance_collection.name for o in inst).most_common(20))


def world_bounds(objs):
    lo, hi = Vector((1e18,) * 3), Vector((-1e18,) * 3)
    n = 0
    for o in objs:
        if o.type != 'MESH':
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
            n += 1
    return (lo, hi) if n else (None, None)


meshes = [o for o in sc.objects if o.type == 'MESH']
lo, hi = world_bounds(meshes)
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
print('[k059] 网格物体', len(meshes), '三角面', tris, '范围', [round(v, 1) for v in lo], [round(v, 1) for v in hi])
print('[k059] 不同网格数据', len({o.data.name for o in meshes}))
hidden = [o for o in meshes if o.hide_render or o.hide_get()]
print('[k059] 隐藏的网格', len(hidden))


def tree(c, depth=0, rows=None):
    rows = rows if rows is not None else []
    objs = list(c.all_objects)
    ms = [o for o in objs if o.type == 'MESH']
    l, h = world_bounds(ms)
    size = [round(h[i] - l[i], 1) for i in range(3)] if l else None
    ctr = [round((h[i] + l[i]) / 2, 1) for i in range(3)] if l else None
    t = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in ms)
    rows.append({'name': c.name, 'depth': depth, 'objects': len(objs), 'meshes': len(ms), 'tris': t, 'size': size,
                 'center': ctr, 'min': [round(v, 2) for v in l] if l else None, 'max': [round(v, 2) for v in h] if h else None,
                 'hide_render': c.hide_render, 'children': [k.name for k in c.children]})
    if depth < 4:
        print('[k059] ' + '  ' * depth + f'{c.name}: {len(objs)} 物体 / {len(ms)} 网格 / {t} 面 尺寸 {size} 中心 {ctr}'
              + (' (不渲染)' if c.hide_render else ''))
    for k in c.children:
        tree(k, depth + 1, rows)
    return rows


rows = tree(sc.collection)

# 每个物体
objs = []
for o in meshes:
    l, h = world_bounds([o])
    objs.append({'name': o.name, 'data': o.data.name, 'coll': o.users_collection[0].name if o.users_collection else '',
                 'min': [round(v, 2) for v in l], 'max': [round(v, 2) for v in h],
                 'tris': sum(len(p.vertices) - 2 for p in o.data.polygons),
                 'mats': [s.material.name if s.material else None for s in o.material_slots],
                 'hidden': bool(o.hide_render or o.hide_get()), 'parent': o.parent.name if o.parent else None})

# 材质 / 贴图
img_rows = []
for i in bpy.data.images:
    fp = bpy.path.abspath(i.filepath) if i.filepath else ''
    img_rows.append({'name': i.name, 'file': i.filepath, 'packed': bool(i.packed_file),
                     'exists': bool(i.packed_file) or (bool(fp) and os.path.exists(fp)), 'size': list(i.size),
                     'users': i.users})
miss = [r for r in img_rows if not r['exists'] and r['file']]
print('[k059] 贴图', len(img_rows), '内嵌', sum(r['packed'] for r in img_rows), '缺失', len(miss))
print('[k059] 贴图尺寸分布', Counter(f"{r['size'][0]}x{r['size'][1]}" for r in img_rows).most_common(10))
if miss:
    print('[k059] 缺失样例', [r['file'] for r in miss[:8]])
mat_rows = {}
node_types = Counter()
for m in bpy.data.materials:
    imgs = []
    if m.use_nodes and m.node_tree:
        for n in m.node_tree.nodes:
            node_types[n.bl_idname] += 1
            if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                imgs.append(n.image.name)
    mat_rows[m.name] = {'users': m.users, 'images': imgs}
print('[k059] 材质', len(mat_rows), '节点类型', node_types.most_common(15))
print('[k059] 材质样例', list(mat_rows.items())[:6])
lights = [o for o in sc.objects if o.type == 'LIGHT']
print('[k059] 灯', [(o.name, o.data.type, round(o.data.energy, 1)) for o in lights[:20]])
print('[k059] 相机', [(o.name, [round(v, 1) for v in o.location]) for o in sc.objects if o.type == 'CAMERA'])
print('[k059] 世界', sc.world.name if sc.world else None)
json.dump({'collections': rows, 'objects': objs, 'images': img_rows, 'materials': mat_rows,
           'bounds': [list(lo), list(hi)], 'tris': tris}, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[k059] 写入', out)
