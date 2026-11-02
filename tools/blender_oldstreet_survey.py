"""老旧街道包普查（只读）：每个物体的集合、世界包围盒、三角面、修改器、材质、父子，每个材质用到的贴图，写 survey.json。
blender -b Inbox/_oldstreet/老旧街道.blend -P tools/blender_oldstreet_survey.py -- Inbox/_oldstreet/work/survey.json
"""
import json
import os
import sys

import bpy
from mathutils import Vector

out = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
sc = bpy.context.scene
deps = bpy.context.evaluated_depsgraph_get()


def colls_of(o):
    return [c.name for c in o.users_collection]


def bounds(o):
    ev = o.evaluated_get(deps)
    lo, hi = [1e18] * 3, [-1e18] * 3
    for c in ev.bound_box:
        w = ev.matrix_world @ Vector(c)
        for i in range(3):
            lo[i] = min(lo[i], w[i])
            hi[i] = max(hi[i], w[i])
    return [round(v, 3) for v in lo], [round(v, 3) for v in hi]


objs = []
for o in sc.objects:
    row = {'name': o.name, 'type': o.type, 'colls': colls_of(o), 'hide_render': o.hide_render, 'hide_view': o.hide_get(),
           'loc': [round(v, 3) for v in o.matrix_world.translation], 'parent': o.parent.name if o.parent else None}
    if o.type == 'MESH':
        lo, hi = bounds(o)
        ev = o.evaluated_get(deps)
        me = ev.to_mesh()
        tris = sum(len(p.vertices) - 2 for p in me.polygons)
        ev.to_mesh_clear()
        row.update(lo=lo, hi=hi, tris=tris, data=o.data.name, users=o.data.users,
                   mods=[(m.type, m.name, m.show_render) for m in o.modifiers],
                   mats=[s.material.name if s.material else None for s in o.material_slots])
    elif o.type == 'EMPTY':
        row.update(inst=o.instance_collection.name if o.instance_type == 'COLLECTION' and o.instance_collection else None)
    elif o.type == 'CAMERA':
        row.update(lens=o.data.lens, rot=[round(v, 3) for v in o.matrix_world.to_euler()])
    elif o.type == 'LIGHT':
        row.update(kind=o.data.type, energy=o.data.energy, color=list(o.data.color))
    objs.append(row)


def walk_nodes(nt, seen=None):
    seen = seen if seen is not None else set()
    if nt is None or nt in seen:
        return []
    seen.add(nt)
    rows = []
    for n in nt.nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            links = []
            for out_ in n.outputs:
                for l in out_.links:
                    links.append(f'{out_.name}->{l.to_node.type}:{l.to_socket.name}')
            rows.append({'img': n.image.name, 'path': n.image.filepath, 'size': list(n.image.size), 'links': links})
        elif n.type == 'GROUP':
            rows += walk_nodes(n.node_tree, seen)
    return rows


mats = {}
for m in bpy.data.materials:
    if not m.use_nodes:
        mats[m.name] = {'users': m.users, 'nodes': False, 'diffuse': list(m.diffuse_color)}
        continue
    bsdf = next((n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    row = {'users': m.users, 'blend': m.blend_method, 'imgs': walk_nodes(m.node_tree), 'types': sorted({n.type for n in m.node_tree.nodes})}
    if bsdf:
        row['base'] = list(bsdf.inputs['Base Color'].default_value)
        row['rough'] = bsdf.inputs['Roughness'].default_value
        row['metal'] = bsdf.inputs['Metallic'].default_value
        row['emis'] = bsdf.inputs['Emission Strength'].default_value if 'Emission Strength' in bsdf.inputs else None
    mats[m.name] = row


def tree(c, d=0):
    return {'name': c.name, 'objs': [o.name for o in c.objects], 'hide_render': c.hide_render, 'children': [tree(k, d + 1) for k in c.children]}


data = {'scene': sc.name, 'scenes': [s.name for s in bpy.data.scenes], 'unit': sc.unit_settings.scale_length,
        'tree': tree(sc.collection), 'all_colls': [c.name for c in bpy.data.collections], 'objects': objs, 'materials': mats,
        'images': {i.name: {'path': i.filepath, 'size': list(i.size), 'users': i.users} for i in bpy.data.images}}
json.dump(data, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('[oldstreet] 写好', out, len(objs), '物体', len(mats), '材质')
