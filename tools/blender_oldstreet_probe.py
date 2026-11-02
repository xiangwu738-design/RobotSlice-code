"""老旧街道：查叶子贴图来源、地面网格顶点和 UV、数组修改器参数、曲线、树叶卡片（只读）。
blender -b Inbox/_oldstreet/老旧街道.blend -P tools/blender_oldstreet_probe.py -- 输出.json
"""
import json, os, sys
import bpy
from mathutils import Vector
out = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
R = {}
for n in ('bq_Leaf_Aesculus-hippocastanum_Diffuse.png', 'bq_Leaf_Aesculus-hippocastanum_Normal.jpg'):
    i = bpy.data.images.get(n)
    R[n] = {'source': i.source, 'gen': i.generated_type if i.source == 'GENERATED' else None, 'packed': i.packed_file is not None, 'path': i.filepath, 'has': i.has_data, 'size': list(i.size)}
deps = bpy.context.evaluated_depsgraph_get()
ground = {}
for n in ['地面.002','地面.003','地面.004','地面.005','地面.006','地面.007','地面.008','地面.009','地面.010']:
    o = bpy.data.objects[n]
    ev = o.evaluated_get(deps)
    me = ev.to_mesh()
    uv = me.uv_layers.active
    verts = [[round(c, 3) for c in (o.matrix_world @ v.co)] for v in me.vertices]
    polys = []
    for p in me.polygons:
        uvs = [[round(x, 3) for x in uv.data[l].uv] for l in p.loop_indices] if uv else None
        polys.append({'v': list(p.vertices), 'uv': uvs, 'mat': p.material_index})
    ground[n] = {'verts': verts, 'polys': polys, 'mods': [(m.type, m.name) for m in o.modifiers]}
    ev.to_mesh_clear()
R['ground'] = ground
mods = {}
for o in bpy.data.objects:
    for m in o.modifiers:
        if m.type == 'ARRAY':
            mods.setdefault(o.name, []).append({'count': m.count, 'fit': m.fit_type, 'len': m.fit_length, 'rel': m.use_relative_offset, 'relv': list(m.relative_offset_displace), 'const': m.use_constant_offset, 'constv': list(m.constant_offset_displace), 'objoff': m.offset_object.name if m.offset_object else None, 'curve': m.curve.name if m.curve else None})
        elif m.type == 'CURVE':
            mods.setdefault(o.name, []).append({'curve': m.object.name if m.object else None, 'axis': m.deform_axis})
        elif m.type == 'NODES':
            mods.setdefault(o.name, []).append({'nodes': m.node_group.name if m.node_group else None})
R['mods'] = mods
# 物体基础变换
tf = {}
for o in bpy.data.objects:
    if o.type in ('MESH','CURVE') and not o.name.startswith('WGT') :
        tf[o.name] = {'mw': [[round(c,4) for c in r] for r in o.matrix_world], 'dims': [round(c,3) for c in o.dimensions]}
R['tf'] = tf
curves = {}
for o in bpy.data.objects:
    if o.type == 'CURVE':
        c = o.data
        curves[o.name] = {'bevel': c.bevel_depth, 'bevobj': c.bevel_object.name if c.bevel_object else None, 'ext': c.extrude, 'dim': c.dimensions, 'splines': [{'type': s.type, 'n': len(s.bezier_points) or len(s.points),
             'pts': [[round(x,3) for x in (o.matrix_world @ (p.co.to_3d() if len(p.co)==4 else p.co))] for p in (s.bezier_points or s.points)][:8]} for s in c.splines][:6], 'mats': [m.name if m else None for m in c.materials], 'nsp': len(c.splines)}
R['curves'] = curves
# 树：原始网格（未加数组）
tree = {}
for n in ['bq_Tree_Aesculus-hippocastanum_D_summer', 'bq_Tree_Aesculus-hippocastanum_D_summer.001']:
    o = bpy.data.objects[n]
    me = o.data
    uv = me.uv_layers.active
    cnt = [0, 0]
    size = 0
    us = []
    for p in me.polygons:
        cnt[p.material_index] += 1
    # 叶子多边形的 UV 范围（前 2000 个）
    lo = [9, 9]; hi = [-9, -9]; areas = []
    k = 0
    for p in me.polygons:
        if p.material_index == 1:
            uvs = [uv.data[l].uv for l in p.loop_indices]
            for u in uvs:
                lo = [min(lo[0], u[0]), min(lo[1], u[1])]
                hi = [max(hi[0], u[0]), max(hi[1], u[1])]
            k += 1
            areas.append(p.area)
            if k >= 20000:
                break
    tree[n] = {'polys': len(me.polygons), 'verts': len(me.vertices), 'bymat': cnt, 'uvlo': lo, 'uvhi': hi, 'area_avg': sum(areas)/max(1,len(areas)), 'origin': list(o.matrix_world.translation), 'scale': list(o.matrix_world.to_scale()), 'bbox': [list(c) for c in o.bound_box][:1], 'dims': list(o.dimensions)}
R['tree'] = tree
json.dump(R, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('[oldstreet] ok')
